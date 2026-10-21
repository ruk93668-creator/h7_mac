from pathlib import Path
import shutil,re
p=Path('Tests/SdramV0/sdram_test.c');s=p.read_text(encoding='utf-8-sig')
s=s.replace('#include "../../CM7/Diagnostics/sdram_check.c"','#include "diagnostics/sdram_check.c"')
s=re.sub(r'EQ\(g_sdram_init_result,[^;]+;','',s)
s=s.replace('EQ(g_sdram_sdclk_hz,110000000);','{PLL2_ClocksTypeDef clocks;HAL_RCCEx_GetPLL2ClockFreq(&clocks);EQ(clocks.PLL2_R_Frequency/2U,110000000);}')
s=s.replace('g_result.sdclk=g_sdram_sdclk_hz;','{PLL2_ClocksTypeDef clocks;HAL_RCCEx_GetPLL2ClockFreq(&clocks);g_result.sdclk=clocks.PLL2_R_Frequency/2U;}')
p.write_text(s,encoding='utf-8')
p=Path('Tests/SdramV0/run_tests.py');runner=p.read_text(encoding='utf-8').replace("'CM7/Diagnostics'","'Tests/SdramV0/diagnostics'");p.write_text(runner,encoding='utf-8')
t=Path('Tests/SdramCleanup');shutil.copyfile('Tests/SdramV0/test.sct',t/'test.sct')
# Standalone historical 1KB diagnostics remain test-only; no production source is copied.
(t/'sdram_test.c').write_text(s.replace('#include "diagnostics/sdram_check.c"','#include "../SdramV0/diagnostics/sdram_check.c"'),encoding='utf-8')
(t/'run_tests.py').write_text(runner,encoding='utf-8')
# A separate executable compiles no diagnostic code at all, validating production startup.
a=s.index('static SysResult_t Test_DevicePhase');prefix=s[:a].replace('#include "diagnostics/sdram_check.c"\n','')
a=s.index('static int VerifyConfig');b=s.index('static int Cases',a);verify=s[a:b]
a=s.index('//启动代码仅用于取得');footer=s[a:]
prod=prefix+'''
static SysResult_t Production_DevicePhase(void)
{
 if(!BSP_SDRAM_IsReady())return SYS_ERR_INVALID_STATE;
 device_phase++;
 return SYS_OK;
}
FRAMEWORK_INIT_REGISTER(Production_DevicePhase,INIT_LEVEL_DEVICE);
'''+verify+'''
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
'''+footer
prod=prod.replace('__HAL_RCC_FMC_CONFIG(c->FmcClockSelection);','__HAL_RCC_FMC_CONFIG(scenario==19?RCC_FMCCLKSOURCE_HCLK:c->FmcClockSelection);')
(t/'production_test.c').write_text(prod,encoding='utf-8')
s=runner.replace("here/'sdram_test", "here/'production_test")
s=s.replace("'commands.json'","'production_commands.json'").replace("'results.json'","'production_results.json'")
a=s.index('names=');b=s.index('\nfields=',a)
names=['正式初始化不读写SDRAM、MPU/时钟/引脚/时序正确','PLL2占用','PLL源错误','HSI分频错误','HSI未就绪','PLL配置IO','PLL配置超时','PLL2R频率不符','HAL SDRAM初始化失败','HAL SDRAM初始化超时','MSP缺失','CLK_ENABLE失败','PALL失败','AUTOREFRESH失败','LOAD_MODE失败','命令超时','刷新设置失败','刷新设置超时','重复Framework初始化无测试无重复配置','FMC选择器不符']
s=s[:a]+'names='+repr(names)+s[b:]
s=s.replace("if id==20:passed=passed and restored and accesses['injected']","passed=passed and accesses['reads']==0 and accesses['writes']==0 and restored")
(t/'run_production.py').write_text(s,encoding='utf-8')
