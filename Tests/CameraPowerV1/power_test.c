#include "../../CM7/BSP/bsp_camera.c"
RCC_TypeDef mock_rcc;
DWT_Type mock_dwt;
CoreDebug_Type mock_debug;
uint32_t mock_pll_ready,mock_frequency,mock_dwt_runs,SystemCoreClock;
//记录真实BSP调用HAL的先后顺序，测试不复制上电实现
static uint32_t trace[128][4];
static uint32_t trace_count;
uint32_t g_failure;
//保存一个HAL调用
static void Add(uint32_t a,uint32_t b,uint32_t c,uint32_t d)
{ trace[trace_count][0]=a;trace[trace_count][1]=b;trace[trace_count][2]=c;trace[trace_count][3]=d;trace_count++; }
//记录输出电平
void HAL_GPIO_WritePin(uint32_t p,uint32_t pin,GPIO_PinState v){Add(1,p,pin,v);}
//记录GPIO模式及上下拉
void HAL_GPIO_Init(uint32_t p,GPIO_InitTypeDef *g){Add(2,p,g->Pin,(g->Mode<<8)|g->Pull);}
//提供空闲SCCB输入
GPIO_PinState HAL_GPIO_ReadPin(uint32_t p,uint32_t pin){(void)p;(void)pin;return GPIO_PIN_SET;}
//提供可配置的PLL1Q状态
void HAL_RCCEx_GetPLL1ClockFreq(PLL1_ClocksTypeDef *c){c->PLL1_Q_Frequency=mock_frequency;}
//记录原有MCO来源和分频
void HAL_RCC_MCOConfig(uint32_t m,uint32_t s,uint32_t d){Add(3,m,s,d);}
//记录等待长度，不模拟真实电源稳定
void HAL_Delay(uint32_t ms){Add(4,ms,0,0);}
//保留测试给定的CPU频率
void SystemCoreClockUpdate(void){}
#define CHECK(x) do{if(!(x)){g_failure=__LINE__;return g_failure;}}while(0)
//检查指定位置的完整HAL调用
static int At(uint32_t n,uint32_t a,uint32_t b,uint32_t c,uint32_t d)
{return n<trace_count&&trace[n][0]==a&&trace[n][1]==b&&trace[n][2]==c&&trace[n][3]==d;}
//执行上电、失败及断电接口专项
uint32_t Power_TestRun(uint32_t id)
{
 uint32_t count; uint8_t value;
 mock_pll_ready=1;mock_rcc.PLLCFGR=1;mock_frequency=240000000U;SystemCoreClock=480000000U;mock_dwt_runs=1;
 if(id==0){
  BSP_Camera_EarlyInit();
  CHECK(At(0,1,GPIOA,GPIO_PIN_15,0));
  CHECK(At(1,2,GPIOA,GPIO_PIN_15,(GPIO_MODE_OUTPUT_PP<<8)|GPIO_PULLDOWN));
  CHECK(At(2,1,GPIOH,GPIO_PIN_13,1));CHECK(At(3,1,GPIOH,GPIO_PIN_15,0));
  count=trace_count;BSP_Camera_EarlyInit();CHECK(trace_count==count);CHECK(!s_camera_ready);return 0;
 }
 if(id>=2&&id<=6){
  if(id==2)mock_pll_ready=0;if(id==3)mock_rcc.PLLCFGR=0;if(id==4)mock_frequency=120000000U;
  if(id==5)SystemCoreClock=0;if(id==6)mock_dwt_runs=0;
  CHECK(BSP_Camera_Init()==(id==6?SYS_ERR_NOT_SUPPORTED:SYS_ERR_INVALID_STATE));
  CHECK(trace_count==7);CHECK(!s_camera_ready);return 0;
 }
 if(id==10){BSP_Camera_PowerEnable();CHECK(At(7,1,GPIOA,GPIO_PIN_15,1));CHECK(!s_camera_ready);return 0;}
 if(id==11){BSP_Camera_PowerEnable();count=trace_count;BSP_Camera_EarlyInit();CHECK(count==trace_count);return 0;}
 if(id==12){BSP_Camera_PowerDisable();CHECK(At(trace_count-1,1,GPIOA,GPIO_PIN_15,0));CHECK(!s_camera_ready);return 0;}
 if(id==13){CHECK(BSP_Camera_ReadReg(0x300a,&value)==SYS_ERR_INVALID_STATE);CHECK(BSP_Camera_WriteReg(0x3008,0)==SYS_ERR_INVALID_STATE);CHECK(trace_count==0);return 0;}
 CHECK(BSP_Camera_Init()==SYS_OK);CHECK(s_camera_ready);
 CHECK(At(7,1,GPIOA,GPIO_PIN_15,1));CHECK(At(8,4,10,0,0));
 CHECK(At(9,1,GPIOH,GPIO_PIN_13,0));CHECK(At(10,3,RCC_MCO1,RCC_MCO1SOURCE_PLL1QCLK,10));
 CHECK(At(11,4,1,0,0));CHECK(At(12,1,GPIOH,GPIO_PIN_15,1));CHECK(At(13,4,20,0,0));
 if(id==9){count=trace_count;CHECK(BSP_Camera_Init()==SYS_OK);CHECK(count==trace_count);}
 if(id==7||id==8){
  BSP_Camera_PowerDisable();CHECK(!s_camera_ready);
  CHECK(At(14,1,GPIOH,GPIO_PIN_15,0));CHECK(At(15,1,GPIOH,GPIO_PIN_13,1));
  CHECK(At(16,2,GPIOA,GPIO_PIN_8,GPIO_MODE_ANALOG<<8));CHECK(At(17,1,GPIOA,GPIO_PIN_15,0));
  CHECK(BSP_Camera_WriteReg(0x3008,0)==SYS_ERR_INVALID_STATE);
  if(id==8){CHECK(BSP_Camera_Init()==SYS_OK);CHECK(s_camera_ready);CHECK(At(18,1,GPIOA,GPIO_PIN_15,1));}
 }
 return 0;
}
