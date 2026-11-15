#include "../../CM7/BSP/bsp_camera.c"
RCC_TypeDef mock_rcc;
DWT_Type mock_dwt;
CoreDebug_Type mock_debug;
uint32_t mock_pll_ready,mock_frequency,mock_dwt_runs,SystemCoreClock;
//为新增I2C边界提供默认成功的时钟和传输结果，原上电断言保持不变
uint32_t mock_i2c_mux,mock_i2c_kernel=120000000U;
static HAL_StatusTypeDef mock_init_status,mock_analog_status,mock_digital_status,mock_tx_status,mock_rx_status;
static uint32_t mock_error,mock_tx_count,mock_rx_count,mock_deinit_count,mock_af;
//记录收发顺序与滤波参数，避免只核对最终调用次数
static uint32_t mock_rx_before_tx,mock_analog_value,mock_digital_value;
static uint32_t mock_tx_addr,mock_tx_size,mock_tx_timeout,mock_rx_addr,mock_rx_size,mock_rx_timeout;
static uint8_t mock_packet[3],mock_read_value=0x56U;

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
void HAL_GPIO_Init(uint32_t p,GPIO_InitTypeDef *g){Add(2,p,g->Pin,(g->Mode<<8)|g->Pull);if(p==GPIOB)mock_af=g->Alternate;}
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

//返回当前共享时钟频率，不修改其他核的配置
uint32_t HAL_RCC_GetPCLK1Freq(void){return mock_i2c_kernel;}
//模拟HAL初始化结果
HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *h){h->ErrorCode=mock_error;return mock_init_status;}
//记录撤销动作
HAL_StatusTypeDef HAL_I2C_DeInit(I2C_HandleTypeDef *h){(void)h;mock_deinit_count++;return HAL_OK;}
//提供模拟滤波配置结果
HAL_StatusTypeDef HAL_I2CEx_ConfigAnalogFilter(I2C_HandleTypeDef *h,uint32_t v){mock_analog_value=v;h->ErrorCode=mock_error;return mock_analog_status;}
//提供数字滤波配置结果
HAL_StatusTypeDef HAL_I2CEx_ConfigDigitalFilter(I2C_HandleTypeDef *h,uint32_t v){mock_digital_value=v;h->ErrorCode=mock_error;return mock_digital_status;}
//记录实际交给HAL的地址、数据和超时
HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *h,uint16_t a,uint8_t *d,uint16_t n,uint32_t t)
{uint32_t i;mock_tx_count++;mock_tx_addr=a;mock_tx_size=n;mock_tx_timeout=t;for(i=0;i<n&&i<3;i++)mock_packet[i]=d[i];h->ErrorCode=mock_error;return mock_tx_status;}
//仅在成功时交付模拟传感器数据
HAL_StatusTypeDef HAL_I2C_Master_Receive(I2C_HandleTypeDef *h,uint16_t a,uint8_t *d,uint16_t n,uint32_t t)
{if(mock_tx_count==0)mock_rx_before_tx=1;mock_rx_count++;mock_rx_addr=a;mock_rx_size=n;mock_rx_timeout=t;h->ErrorCode=mock_error;if(mock_rx_status==HAL_OK)*d=mock_read_value;return mock_rx_status;}
//提供HAL原始错误位
uint32_t HAL_I2C_GetError(I2C_HandleTypeDef *h){return h->ErrorCode;}

//验证硬件I2C配置、SCCB报文及错误传播，复用真实BSP
uint32_t I2c_TestRun(uint32_t id)
{
 uint8_t value=0xAAU;uint32_t count;
 if(id==11){CHECK(BSP_Camera_ReadReg(0x300A,0)==SYS_ERR_INVALID_ARG);CHECK(mock_tx_count==0);return 0;}
 if(id>=12&&id<=16){
  mock_pll_ready=1;mock_rcc.PLLCFGR=1;mock_frequency=240000000U;SystemCoreClock=480000000U;mock_dwt_runs=1;
  if(id==12)mock_init_status=HAL_ERROR;if(id==13)mock_analog_status=HAL_ERROR;if(id==14)mock_digital_status=HAL_ERROR;
  if(id==15)mock_i2c_mux=1;if(id==16)mock_i2c_kernel=64000000U;
  CHECK(BSP_Camera_Init()==(id>=15?SYS_ERR_INVALID_STATE:SYS_ERR_IO));
  CHECK(!s_camera_ready&&!s_camera_i2c_ready);CHECK(trace_count==7);
  if(id==13||id==14)CHECK(mock_deinit_count==1);return 0;
 }
 if(id==19){BSP_Camera_PowerEnable();CHECK(BSP_Camera_ReadReg(0x300A,&value)==SYS_ERR_INVALID_STATE);CHECK(mock_tx_count==0);return 0;}
 CHECK(Power_TestRun(1)==0);
 if(id==0){CHECK(CAMERA_SDA==GPIO_PIN_9&&CAMERA_SCL==GPIO_PIN_8);CHECK(mock_af==GPIO_AF4_I2C1);CHECK(At(6,2,GPIOB,GPIO_PIN_8|GPIO_PIN_9,GPIO_MODE_AF_OD<<8));CHECK(s_camera_i2c.Init.Timing==0xB0D32C36U);CHECK(s_camera_i2c.Instance==I2C1);CHECK(mock_analog_value==I2C_ANALOGFILTER_ENABLE&&mock_digital_value==0);return 0;}
 if(id==17){CHECK(mock_i2c_mux==0&&mock_i2c_kernel==120000000U);return 0;}
 if(id==18||id==25){BSP_Camera_PowerDisable();CHECK(mock_deinit_count==1);CHECK(!s_camera_i2c_ready);CHECK(BSP_Camera_ReadReg(0x300A,&value)==SYS_ERR_INVALID_STATE);CHECK(BSP_Camera_WriteReg(0x3008,0x82)==SYS_ERR_INVALID_STATE);CHECK(mock_tx_count==0);return 0;}
 if(id==2||id==3||id==4){
  count=trace_count;
  if(id==4){mock_tx_status=HAL_ERROR;mock_error=HAL_I2C_ERROR_AF;}
  CHECK(BSP_Camera_WriteReg(0x3008,id==2?0x42:0x82)==(id==4?SYS_ERR_IO:SYS_OK));
  CHECK(mock_tx_count==1&&mock_rx_count==0&&mock_tx_addr==0x78&&mock_tx_size==3&&mock_tx_timeout==20);
  CHECK(mock_packet[0]==0x30&&mock_packet[1]==8&&mock_packet[2]==(id==2?0x42:0x82));
  CHECK(trace_count==count+(id==3?1:0));if(id==3)CHECK(At(count,4,5,0,0));return 0;
 }
 if(id==5||id==23){mock_tx_status=HAL_ERROR;mock_error=HAL_I2C_ERROR_AF;CHECK(BSP_Camera_ReadReg(0x300A,&value)==SYS_ERR_IO);CHECK(mock_rx_count==0&&value==0xAA);CHECK(s_sccb_error==HAL_I2C_ERROR_AF);if(id==5)return 0;mock_tx_status=HAL_OK;mock_error=0;}
 if(id==6){mock_rx_status=HAL_ERROR;mock_error=HAL_I2C_ERROR_AF;CHECK(BSP_Camera_ReadReg(0x300A,&value)==SYS_ERR_IO);CHECK(value==0xAA&&mock_rx_count==1);return 0;}
 if(id>=7&&id<=10){
  mock_error=id==9?HAL_I2C_ERROR_TIMEOUT:HAL_I2C_ERROR_BERR;
  mock_tx_status=id==7?HAL_BUSY:(id==8?HAL_TIMEOUT:HAL_ERROR);
  CHECK(BSP_Camera_ReadReg(0x300A,&value)==(id==7?SYS_ERR_BUSY:(id==10?SYS_ERR_IO:SYS_ERR_TIMEOUT)));
  CHECK(mock_rx_count==0);if(id==7)CHECK(s_sccb_error==0);return 0;
 }
 CHECK(BSP_Camera_ReadReg(id==20?0xFFFF:0x300A,&value)==SYS_OK);
 CHECK(value==0x56&&mock_tx_addr==0x78&&mock_rx_addr==0x78&&mock_tx_size==2&&mock_rx_size==1);
 CHECK(mock_tx_timeout==20&&mock_rx_timeout==20);CHECK(mock_rx_before_tx==0);
 CHECK(mock_packet[0]==(id==20?0xFF:0x30)&&mock_packet[1]==(id==20?0xFF:0x0A));
 if(id==21){mock_read_value=0x40;CHECK(BSP_Camera_ReadReg(0x300B,&value)==SYS_OK);CHECK(value==0x40&&mock_packet[1]==0x0B);}
 CHECK(s_sccb_error==0);return 0;
}
