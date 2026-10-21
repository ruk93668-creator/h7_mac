#define main Sdram_FirmwareMain
#include "../../../CM7/Core/Src/main.c"
#undef main
#include "../../../CM7/BSP/bsp_sdram.c"
#include "../../../CM7/Framework/framework_init.c"
#include <stddef.h>

void *memcpy(void *dst,const void *src,size_t n){uint8_t *d=dst;const uint8_t *s=src;while(n--)*d++=*s++;return dst;}
void *memset(void *dst,int v,size_t n){uint8_t *d=dst;while(n--)*d++=(uint8_t)v;return dst;}
void __aeabi_memclr4(void *dst,size_t n){volatile uint8_t *d=dst;while(n--)*d++=0;}
void __aeabi_memclr8(void *dst,size_t n){volatile uint8_t *d=dst;while(n--)*d++=0;}
void __aeabi_memclr(void *dst,size_t n){volatile uint8_t *d=dst;while(n--)*d++=0;}
void __aeabi_memcpy(void *dst,const void *src,size_t n){volatile uint8_t *d=dst;const volatile uint8_t *s=src;while(n--)*d++=*s++;}
void __aeabi_memcpy4(void *dst,const void *src,size_t n){volatile uint8_t *d=dst;const volatile uint8_t *s=src;while(n--)*d++=*s++;}
int strcmp(const char *a,const char *b){while(*a && *a==*b){a++;b++;}return (unsigned char)*a-(unsigned char)*b;}

typedef struct {
 uint32_t fail_line,checks,clock_calls,gpio_calls,hal_init_calls,command_calls,refresh_calls,delay_ms;
 uint32_t check_state,words,failed_address,expected,actual,sdclk;
 int32_t init_result,check_result;
} TestResult;
volatile TestResult g_result;
static MPU_Region_InitTypeDef mpu_regions[3];
static uint32_t mpu_seen,mpu_disabled,mpu_enabled,scenario,device_phase;
static RCC_PeriphCLKInitTypeDef observed_clock;
static FMC_SDRAM_TimingTypeDef observed_timing;
static FMC_SDRAM_InitTypeDef observed_init;
static FMC_SDRAM_CommandTypeDef commands[4];
static const uint32_t initial_pattern=0x13579BDFU;
uint32_t SystemCoreClock;
static int Check(uint32_t line,int32_t a,int32_t b){g_result.checks++;if(a!=b){if(!g_result.fail_line)g_result.fail_line=line;return 0;}return 1;}
#define EQ(a,b) do{if(!Check(__LINE__,(int32_t)(a),(int32_t)(b)))return 1;}while(0)
void HAL_MPU_Disable(void){mpu_disabled++;}
void HAL_MPU_Enable(uint32_t control){if(control!=MPU_PRIVILEGED_DEFAULT)g_result.fail_line=__LINE__;mpu_enabled++;}
void HAL_MPU_ConfigRegion(const MPU_Region_InitTypeDef *r){if(r->Number>2){g_result.fail_line=__LINE__;return;}mpu_regions[r->Number]=*r;mpu_seen|=1U<<r->Number;}
HAL_StatusTypeDef HAL_RCCEx_PeriphCLKConfig(RCC_PeriphCLKInitTypeDef *c)
{
 g_result.clock_calls++;observed_clock=*c;
 if(mpu_seen!=7)g_result.fail_line=__LINE__;
 if(scenario==5)return HAL_ERROR;
 if(scenario==6)return HAL_TIMEOUT;
 RCC->CR|=RCC_CR_PLL2ON|RCC_CR_PLL2RDY;
 RCC->PLLCKSELR|=c->PLL2.PLL2M<<12;
 RCC->PLL2DIVR=(c->PLL2.PLL2N-1)|((c->PLL2.PLL2P-1)<<9)|((c->PLL2.PLL2Q-1)<<16)|(((scenario==7?4:c->PLL2.PLL2R)-1)<<24);
 RCC->PLL2FRACR=0;
 __HAL_RCC_FMC_CONFIG(scenario==19?RCC_FMCCLKSOURCE_HCLK:c->FmcClockSelection);
 return HAL_OK;
}
void HAL_GPIO_Init(GPIO_TypeDef *port,const GPIO_InitTypeDef *g)
{
 static GPIO_TypeDef * const ports[]={GPIOD,GPIOE,GPIOF,GPIOG,GPIOH};
 static const uint32_t pins[]={0xC703U,0xFF83U,0xF83FU,0x8137U,0xE0U};
 uint32_t n=g_result.gpio_calls++;
 if(n>=5 || port!=ports[n] || g->Pin!=pins[n] || g->Mode!=GPIO_MODE_AF_PP || g->Alternate!=GPIO_AF12_FMC || g->Speed!=GPIO_SPEED_FREQ_VERY_HIGH || g->Pull!=GPIO_NOPULL)g_result.fail_line=__LINE__;
}
HAL_StatusTypeDef HAL_SDRAM_Init(SDRAM_HandleTypeDef *h,FMC_SDRAM_TimingTypeDef *timing)
{
 g_result.hal_init_calls++;observed_init=h->Init;observed_timing=*timing;
 if(scenario==8)return HAL_ERROR;
 if(scenario==9)return HAL_TIMEOUT;
 if(scenario!=10 && h->State==HAL_SDRAM_STATE_RESET)HAL_SDRAM_MspInit(h);
 h->State=HAL_SDRAM_STATE_READY;
 return HAL_OK;
}
HAL_StatusTypeDef HAL_SDRAM_SendCommand(SDRAM_HandleTypeDef *h,FMC_SDRAM_CommandTypeDef *command,uint32_t timeout)
{
 uint32_t n=g_result.command_calls++;
 if(h!=&s_sdram || n>=4 || timeout!=100)g_result.fail_line=__LINE__;
 if(n<4)commands[n]=*command;
 if(scenario>=11 && scenario<=14 && n==scenario-11)return HAL_ERROR;
 if(scenario==15 && n==1)return HAL_TIMEOUT;
 if(n==1 && g_result.delay_ms<1)g_result.fail_line=__LINE__;
 return HAL_OK;
}
HAL_StatusTypeDef HAL_SDRAM_ProgramRefreshRate(SDRAM_HandleTypeDef *h,uint32_t count)
{
 g_result.refresh_calls++;
 if(h!=&s_sdram || count!=839 || g_result.command_calls!=4)g_result.fail_line=__LINE__;
 return scenario==16?HAL_ERROR:scenario==17?HAL_TIMEOUT:HAL_OK;
}
void HAL_Delay(uint32_t ms){g_result.delay_ms+=ms;}

static SysResult_t Production_DevicePhase(void)
{
 if(!BSP_SDRAM_IsReady())return SYS_ERR_INVALID_STATE;
 device_phase++;
 return SYS_OK;
}
FRAMEWORK_INIT_REGISTER(Production_DevicePhase,INIT_LEVEL_DEVICE);
static int VerifyConfig(void)
{
 uint32_t i;
 EQ(mpu_seen,7);EQ(mpu_enabled,2);EQ(mpu_disabled,2);
 EQ(mpu_regions[0].SubRegionDisable,0x87);EQ(mpu_regions[0].AccessPermission,MPU_REGION_NO_ACCESS);
 EQ(mpu_regions[1].BaseAddress,0x38000000);EQ(mpu_regions[1].Size,MPU_REGION_SIZE_4KB);
 EQ(mpu_regions[2].BaseAddress,0xD0000000);EQ(mpu_regions[2].Size,MPU_REGION_SIZE_128KB);
 EQ(mpu_regions[2].Enable,MPU_REGION_ENABLE);EQ(mpu_regions[2].SubRegionDisable,0);
 EQ(mpu_regions[2].TypeExtField,MPU_TEX_LEVEL1);EQ(mpu_regions[2].IsCacheable,MPU_ACCESS_NOT_CACHEABLE);
 EQ(mpu_regions[2].IsBufferable,MPU_ACCESS_NOT_BUFFERABLE);EQ(mpu_regions[2].IsShareable,MPU_ACCESS_SHAREABLE);
 EQ(mpu_regions[2].DisableExec,MPU_INSTRUCTION_ACCESS_DISABLE);EQ(mpu_regions[2].AccessPermission,MPU_REGION_FULL_ACCESS);
 EQ(observed_clock.PeriphClockSelection,RCC_PERIPHCLK_FMC|RCC_PERIPHCLK_PLL2_DIVR);
 EQ(observed_clock.FmcClockSelection,RCC_FMCCLKSOURCE_PLL2);
 EQ(observed_clock.PLL2.PLL2M,16);EQ(observed_clock.PLL2.PLL2N,165);EQ(observed_clock.PLL2.PLL2R,3);
 EQ(observed_clock.PLL2.PLL2P,6);EQ(observed_clock.PLL2.PLL2Q,6);
 EQ(observed_clock.PLL2.PLL2RGE,RCC_PLL2VCIRANGE_2);EQ(observed_clock.PLL2.PLL2VCOSEL,RCC_PLL2VCOWIDE);EQ(observed_clock.PLL2.PLL2FRACN,0);
 EQ(observed_init.SDBank,FMC_SDRAM_BANK2);EQ(observed_init.ColumnBitsNumber,FMC_SDRAM_COLUMN_BITS_NUM_10);
 EQ(observed_init.RowBitsNumber,FMC_SDRAM_ROW_BITS_NUM_13);EQ(observed_init.MemoryDataWidth,FMC_SDRAM_MEM_BUS_WIDTH_16);
 EQ(observed_init.InternalBankNumber,FMC_SDRAM_INTERN_BANKS_NUM_4);EQ(observed_init.CASLatency,FMC_SDRAM_CAS_LATENCY_2);
 EQ(observed_init.WriteProtection,FMC_SDRAM_WRITE_PROTECTION_DISABLE);EQ(observed_init.SDClockPeriod,FMC_SDRAM_CLOCK_PERIOD_2);
 EQ(observed_init.ReadBurst,FMC_SDRAM_RBURST_DISABLE);EQ(observed_init.ReadPipeDelay,FMC_SDRAM_RPIPE_DELAY_0);
 EQ(observed_timing.LoadToActiveDelay,2);EQ(observed_timing.ExitSelfRefreshDelay,10);EQ(observed_timing.SelfRefreshTime,6);
 EQ(observed_timing.RowCycleDelay,9);EQ(observed_timing.WriteRecoveryTime,3);EQ(observed_timing.RPDelay,3);EQ(observed_timing.RCDDelay,3);
 EQ(commands[0].CommandMode,FMC_SDRAM_CMD_CLK_ENABLE);EQ(commands[1].CommandMode,FMC_SDRAM_CMD_PALL);
 EQ(commands[2].CommandMode,FMC_SDRAM_CMD_AUTOREFRESH_MODE);EQ(commands[3].CommandMode,FMC_SDRAM_CMD_LOAD_MODE);
 EQ(commands[2].AutoRefreshNumber,8);EQ(commands[3].ModeRegisterDefinition,0x220);
 for(i=0;i<4;i++)EQ(commands[i].CommandTarget,FMC_SDRAM_CMD_TARGET_BANK2);
 EQ(g_result.gpio_calls,5);EQ(g_result.refresh_calls,1);{PLL2_ClocksTypeDef clocks;HAL_RCCEx_GetPLL2ClockFreq(&clocks);EQ(clocks.PLL2_R_Frequency/2U,110000000);}
 return 0;
}

static int Cases(uint32_t id)
{
 uint32_t i;
 SysResult_t result,expected=SYS_ERR_IO;
 const FrameworkInitEntry_t *entry;
 uint32_t count=0;
 scenario=id;
 RCC->CR=RCC_CR_HSION|RCC_CR_HSIRDY|RCC_CR_PLL1ON|RCC_CR_PLL1RDY;
 RCC->PLLCKSELR=RCC_PLLSOURCE_HSI;
 RCC->PLL1DIVR=0x01013B01;
 RCC->D2CCIP2R=0xDEADBEEF;
 MPU_Config();MPU_ConfigSdram();
 EQ(HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_FMC),0);
 EQ(BSP_SDRAM_IsReady(),0);
 //初始化表只有正式BSP入口和测试观察入口，不含MEDIA诊断
 for(entry=Image$$ER_FW_INIT$$Base;entry<Image$$ER_FW_INIT$$Limit;entry++){
  EQ(entry->level==INIT_LEVEL_BUS || entry->level==INIT_LEVEL_DEVICE,1);count++;
 }
 EQ(count,2);
 if(id==1)RCC->CR|=RCC_CR_PLL2ON;
 if(id==2)RCC->PLLCKSELR=RCC_PLLSOURCE_HSE;
 if(id==3)RCC->CR|=RCC_CR_HSIDIV_2;
 if(id==4)RCC->CR&=~RCC_CR_HSIRDY;
 result=Framework_InitRuntime();g_result.init_result=result;
 EQ(RCC->PLL1DIVR,0x01013B01);EQ(RCC->D2CCIP2R,0xDEADBEEF);
 if((id>=1 && id<=17)||id==19){
  if(id==1)expected=SYS_ERR_BUSY;
  if((id>=2 && id<=4)||id==7||id==10||id==19)expected=SYS_ERR_INVALID_STATE;
  if(id==6||id==9||id==15||id==17)expected=SYS_ERR_TIMEOUT;
  EQ(result,expected);EQ(BSP_SDRAM_IsReady(),0);EQ(device_phase,0);
  if(id<=6)EQ(g_result.hal_init_calls,0);
  if(id>=11 && id<=14)EQ(g_result.command_calls,id-10);
  return 0;
 }
 EQ(result,SYS_OK);EQ(BSP_SDRAM_IsReady(),1);EQ(device_phase,1);EQ(VerifyConfig(),0);
 for(i=0;i<100;i++)EQ(BSP_SDRAM_IsReady(),1);
 if(id==18){
  EQ(Framework_InitRuntime(),SYS_OK);EQ(device_phase,2);
  EQ(g_result.command_calls,4);EQ(g_result.hal_init_calls,1);EQ(g_result.clock_calls,1);
 }
 return 0;
}
int Sdram_TestRun(uint32_t id)
{
 int result=Cases(id);
 PLL2_ClocksTypeDef clocks;
 HAL_RCCEx_GetPLL2ClockFreq(&clocks);g_result.sdclk=clocks.PLL2_R_Frequency/2U;
 return result;
}
//启动代码仅用于取得真实MPU函数，测试不执行双核硬件启动
void App_Run(void){}
void Framework_Run(void){}
void BSP_Camera_EarlyInit(void){}
void IPC_Service_EarlyInit(void){}
HAL_StatusTypeDef HAL_HSEM_FastTake(uint32_t id){(void)id;return HAL_OK;}
void HAL_HSEM_Release(uint32_t id,uint32_t pid){(void)id;(void)pid;}
HAL_StatusTypeDef HAL_Init(void){return HAL_OK;}
HAL_StatusTypeDef HAL_PWREx_ConfigSupply(uint32_t supply){(void)supply;return HAL_OK;}
void HAL_RCCEx_EnableBootCore(uint32_t core){(void)core;}
HAL_StatusTypeDef HAL_RCC_ClockConfig(const RCC_ClkInitTypeDef *clock,uint32_t latency){(void)clock;(void)latency;return HAL_OK;}
HAL_StatusTypeDef HAL_RCC_OscConfig(RCC_OscInitTypeDef *clock){(void)clock;return HAL_OK;}


//使用当前HAL源码的真实查询函数，不再Mock FMC频率为220MHz
#include "hal_clock_queries.inc"
void HAL_RCCEx_GetPLL1ClockFreq(PLL1_ClocksTypeDef *c){c->PLL1_P_Frequency=0;c->PLL1_Q_Frequency=0;c->PLL1_R_Frequency=0;}
void HAL_RCCEx_GetPLL3ClockFreq(PLL3_ClocksTypeDef *c){c->PLL3_P_Frequency=0;c->PLL3_Q_Frequency=0;c->PLL3_R_Frequency=0;}
uint32_t HAL_RCC_GetHCLKFreq(void){return 240000000;}
uint32_t HAL_RCC_GetPCLK1Freq(void){return 120000000;}
uint32_t HAL_RCC_GetPCLK2Freq(void){return 120000000;}

uint32_t HAL_RCCEx_GetD3PCLK1Freq(void){return 120000000;}

//SDRAM隔离回归不执行双核启动，引脚预配置由DCMI专门用例验收
void BSP_DCMI_EarlyInit(void){}
