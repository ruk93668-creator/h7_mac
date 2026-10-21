#include "stm32h7xx_hal.h"
static HAL_StatusTypeDef Test_DMA_Init(DMA_HandleTypeDef *h);
static HAL_StatusTypeDef Test_DCMI_Init(DCMI_HandleTypeDef *h);
static HAL_StatusTypeDef Test_DMA_DeInit(DMA_HandleTypeDef *h);
static HAL_StatusTypeDef Test_Start(DCMI_HandleTypeDef *h,uint32_t mode,uint32_t address,uint32_t length);
static HAL_StatusTypeDef Test_Stop(DCMI_HandleTypeDef *h);
static HAL_StatusTypeDef Test_DcmiDeInit(DCMI_HandleTypeDef *h);
static uint32_t Test_Primask(void);
static uint32_t Test_Ipsr(void);
static void Test_DisableIrq(void);
static void Test_SetPrimask(uint32_t value);
#define HAL_DMA_Init Test_DMA_Init
#define HAL_DCMI_Init Test_DCMI_Init
#define HAL_DMA_DeInit Test_DMA_DeInit
#define HAL_DCMI_Start_DMA Test_Start
#define HAL_DCMI_Stop Test_Stop
#define HAL_DCMI_DeInit Test_DcmiDeInit
#define __get_PRIMASK Test_Primask
#define __get_IPSR Test_Ipsr
#define __disable_irq Test_DisableIrq
#define __set_PRIMASK Test_SetPrimask
#include "../../CM7/BSP/bsp_dcmi.c"
#undef HAL_DMA_Init
#undef HAL_DCMI_Init
#undef HAL_DMA_DeInit
#undef HAL_DCMI_Start_DMA
#undef HAL_DCMI_Stop
#undef HAL_DCMI_DeInit
#undef __get_PRIMASK
#undef __get_IPSR
#undef __disable_irq
#undef __set_PRIMASK
#include "../../CM7/Framework/framework_init.c"
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
 uint32_t fail_line,checks,dma_init,dcmi_init,dma_deinit,nvic_enabled,nvic_cleared,dma_callback,dcmi_callback;
 uint32_t dma_cr,dcmi_cr,request,ready;
 int32_t result;
 uint32_t event_flags,dcmi_error,dma_error,stop_required,primask,active,start_calls,stop_calls;
} TestResult;
volatile TestResult g_result;
uint32_t SystemCoreClock=480000000;
static uint32_t scenario,inject,pending[2],priority[2],subpriority[2],tick;
static uint32_t mock_primask,mock_ipsr,fault_start,fault_stop,fault_dma_init,fault_dma_deinit,fault_dcmi_init,fault_dcmi_deinit;
static uint32_t arm_deferred,deferred;
volatile uint32_t g_stuck_stream;
static void DeliverDeferred(void);
static int Check(uint32_t line,int32_t a,int32_t b){g_result.checks++;if(a!=b){if(!g_result.fail_line)g_result.fail_line=line;return 0;}return 1;}
#define EQ(a,b) do{if(!Check(__LINE__,(int32_t)(a),(int32_t)(b)))return 1;}while(0)
uint32_t HAL_GetTick(void){return tick++;}
static HAL_StatusTypeDef Test_DMA_Init(DMA_HandleTypeDef *h)
{
 g_result.dma_init++;
 if(fault_dma_init)return HAL_ERROR;
 if(h!=&s_dcmi_dma)g_result.fail_line=__LINE__;
 if(inject==6 || inject==13)return HAL_ERROR;
 if(inject==7)return HAL_BUSY;
 if(inject==8)return HAL_TIMEOUT;
 return HAL_DMA_Init(h);
}
static HAL_StatusTypeDef Test_DCMI_Init(DCMI_HandleTypeDef *h)
{
 g_result.dcmi_init++;
 if(fault_dcmi_init)return HAL_ERROR;
 if(h!=&s_dcmi || h->DMA_Handle!=&s_dcmi_dma || s_dcmi_dma.Parent!=h)g_result.fail_line=__LINE__;
 if(inject==9 || inject==12 || inject==14)return HAL_ERROR;
 if(inject==10)return HAL_BUSY;
 if(inject==11)return HAL_TIMEOUT;
 return HAL_DCMI_Init(h);
}
static HAL_StatusTypeDef Test_DMA_DeInit(DMA_HandleTypeDef *h)
{
 g_result.dma_deinit++;
 if(fault_dma_deinit)return HAL_ERROR;
 if(inject==12)return HAL_ERROR;
 return HAL_DMA_DeInit(h);
}
static uint32_t Index(IRQn_Type irq){if(irq==DMA1_Stream0_IRQn)return 0;if(irq==DCMI_IRQn)return 1;g_result.fail_line=__LINE__;return 0;}
void HAL_NVIC_DisableIRQ(IRQn_Type irq){g_result.nvic_enabled&=~(1U<<Index(irq));}
void HAL_NVIC_ClearPendingIRQ(IRQn_Type irq){uint32_t i=Index(irq);pending[i]=0;g_result.nvic_cleared|=1U<<i;}
void HAL_NVIC_SetPriority(IRQn_Type irq,uint32_t preempt,uint32_t sub){uint32_t i=Index(irq);priority[i]=preempt;subpriority[i]=sub;}
void HAL_NVIC_EnableIRQ(IRQn_Type irq)
{
 uint32_t i=Index(irq);
 if(pending[i] || s_dcmi.State!=HAL_DCMI_STATE_READY || s_dcmi_dma.State!=HAL_DMA_STATE_READY)g_result.fail_line=__LINE__;
 g_result.nvic_enabled|=1U<<i;
}
void HAL_DCMI_LineEventCallback(DCMI_HandleTypeDef *h){if(h!=&s_dcmi)g_result.fail_line=__LINE__;g_result.dcmi_callback++;}

static GPIO_TypeDef *const ports[]={GPIOA,GPIOB,GPIOC,GPIOE,GPIOG,GPIOH};
static const uint32_t pins[]={GPIO_PIN_4|GPIO_PIN_6,GPIO_PIN_6|GPIO_PIN_7,GPIO_PIN_6|GPIO_PIN_7,GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6,GPIO_PIN_10,GPIO_PIN_12};
static uint32_t saved[6][7];
static void Seed(void)
{
 uint32_t i,j;volatile uint32_t *reg;
 for(i=0;i<6;i++){
  reg=&ports[i]->MODER;
  for(j=0;j<7;j++){reg[j]=0xA55AA55AU^(i*0x11111111U);saved[i][j]=reg[j];}
  ports[i]->AFR[0]=0x44444444U;ports[i]->AFR[1]=0xBBBBBBBBU;
 }
 RCC->PLL1DIVR=0x0BADBEEF;RCC->PLL2DIVR=0x01234567;RCC->D2CCIP2R=0xDEADBEEF;
 pending[0]=pending[1]=1;inject=scenario;
}
static int Pins(void)
{
 uint32_t i,j,mask,mode_mask;
 for(i=0;i<6;i++){
  mask=mode_mask=0;
  for(j=0;j<16;j++)if(pins[i]&(1U<<j)){
   mask|=3UL<<(2*j);mode_mask|=1U<<j;
   EQ((ports[i]->MODER>>(2*j))&3U,2);
   EQ((ports[i]->OSPEEDR>>(2*j))&3U,3);
   EQ((ports[i]->PUPDR>>(2*j))&3U,0);
   EQ((ports[i]->OTYPER>>j)&1U,0);
   EQ((ports[i]->AFR[j/8]>>(4*(j%8)))&15U,13);
  }
  EQ(ports[i]->MODER&~mask,saved[i][0]&~mask);
  EQ(ports[i]->OTYPER&~mode_mask,saved[i][1]&~mode_mask);
  EQ(ports[i]->OSPEEDR&~mask,saved[i][2]&~mask);
  EQ(ports[i]->PUPDR&~mask,saved[i][3]&~mask);
 }
 //原PB8/9 SCCB、PB10/11 I2C和SDRAM引脚不在DCMI掩码内
 EQ((GPIOB->AFR[1]>>8)&255U,0xBB);
 return 0;
}
static int Config(void)
{
 EQ(s_dcmi_ready,1);EQ(s_dcmi_pins_ready,1);EQ(Pins(),0);
 EQ(s_dcmi_dma.Instance==DMA1_Stream0,1);EQ(s_dcmi.Instance==DCMI,1);
 EQ(s_dcmi.DMA_Handle==&s_dcmi_dma,1);EQ(s_dcmi_dma.Parent==&s_dcmi,1);
 EQ(s_dcmi_dma.Init.Request,DMA_REQUEST_DCMI);EQ(s_dcmi_dma.Init.Direction,DMA_PERIPH_TO_MEMORY);
 EQ(s_dcmi_dma.Init.PeriphInc,DMA_PINC_DISABLE);EQ(s_dcmi_dma.Init.MemInc,DMA_MINC_ENABLE);
 EQ(s_dcmi_dma.Init.PeriphDataAlignment,DMA_PDATAALIGN_WORD);EQ(s_dcmi_dma.Init.MemDataAlignment,DMA_MDATAALIGN_WORD);
 EQ(s_dcmi_dma.Init.Mode,DMA_NORMAL);EQ(s_dcmi_dma.Init.Priority,DMA_PRIORITY_HIGH);EQ(s_dcmi_dma.Init.FIFOMode,DMA_FIFOMODE_DISABLE);
 EQ(s_dcmi.Init.SynchroMode,DCMI_SYNCHRO_HARDWARE);EQ(s_dcmi.Init.PCKPolarity,DCMI_PCKPOLARITY_RISING);
 EQ(s_dcmi.Init.VSPolarity,DCMI_VSPOLARITY_HIGH);EQ(s_dcmi.Init.HSPolarity,DCMI_HSPOLARITY_HIGH);
 EQ(s_dcmi.Init.CaptureRate,DCMI_CR_ALL_FRAME);EQ(s_dcmi.Init.ExtendedDataMode,DCMI_EXTEND_DATA_8B);EQ(s_dcmi.Init.JPEGMode,DCMI_JPEG_DISABLE);
 EQ(DCMI->CR&(DCMI_CR_CAPTURE|DCMI_CR_ENABLE),0);EQ(DMA1_Stream0->CR&DMA_SxCR_EN,0);
 EQ(DCMI->CR&(DCMI_CR_PCKPOL|DCMI_CR_HSPOL|DCMI_CR_VSPOL|DCMI_CR_ESS|DCMI_CR_EDM_0|DCMI_CR_EDM_1),DCMI_HSPOLARITY_HIGH|DCMI_VSPOLARITY_HIGH|DCMI_PCKPOLARITY_RISING);
 EQ(DMA1_Stream0->NDTR,0);EQ(DMA1_Stream0->PAR,0);EQ(DMA1_Stream0->M0AR,0);EQ(DMA1_Stream0->M1AR,0);
 EQ(s_dcmi_dma.DMAmuxChannel->CCR&DMAMUX_CxCR_DMAREQ_ID,DMA_REQUEST_DCMI);
 EQ(DMA1_Stream0->CR&(DMA_SxCR_PSIZE|DMA_SxCR_MSIZE|DMA_SxCR_MINC|DMA_SxCR_PL),DMA_PDATAALIGN_WORD|DMA_MDATAALIGN_WORD|DMA_MINC_ENABLE|DMA_PRIORITY_HIGH);
 EQ(s_dcmi.State,HAL_DCMI_STATE_READY);EQ(s_dcmi_dma.State,HAL_DMA_STATE_READY);
 EQ(g_result.nvic_enabled,3);EQ(g_result.nvic_cleared,3);
 EQ(priority[0],5);EQ(priority[1],5);EQ(subpriority[0],0);EQ(subpriority[1],0);
 EQ(RCC->PLL1DIVR,0x0BADBEEF);EQ(RCC->PLL2DIVR,0x01234567);EQ(RCC->D2CCIP2R,0xDEADBEEF);
 return 0;
}

uint32_t Platform_TimeGetMs(void){return tick++;}
static uint32_t Test_Primask(void){return mock_primask;}
static uint32_t Test_Ipsr(void){return mock_ipsr;}
static void Test_DisableIrq(void){mock_primask=1;if(arm_deferred){deferred=arm_deferred;arm_deferred=0;}}
static void Test_SetPrimask(uint32_t value){mock_primask=value;if(!value && deferred)DeliverDeferred();}
static HAL_StatusTypeDef Test_Start(DCMI_HandleTypeDef *h,uint32_t mode,uint32_t address,uint32_t length)
{
 g_result.start_calls++;
 if(fault_start){h->Instance->CR|=DCMI_CR_ENABLE;return fault_start==1?HAL_ERROR:fault_start==2?HAL_BUSY:HAL_TIMEOUT;}
 return HAL_DCMI_Start_DMA(h,mode,address,length);
}
static HAL_StatusTypeDef Test_Stop(DCMI_HandleTypeDef *h)
{
 g_result.stop_calls++;
 if(fault_stop==1)return HAL_OK;
 if(fault_stop==2)return HAL_TIMEOUT;
 if(fault_stop==3)return HAL_ERROR;
 return HAL_DCMI_Stop(h);
}
static HAL_StatusTypeDef Test_DcmiDeInit(DCMI_HandleTypeDef *h){return fault_dcmi_deinit?HAL_ERROR:HAL_DCMI_DeInit(h);}
static void DeliverDeferred(void)
{
 uint32_t what=deferred;deferred=0;
 if(what==1){DMA1_Stream0->CR&=~DMA_SxCR_EN;DMA1_Stream0->NDTR=0;s_dcmi_dma.State=HAL_DMA_STATE_READY;DCMI->CR&=~DCMI_CR_CAPTURE;HAL_DCMI_FrameEventCallback(&s_dcmi);}
 else{s_dcmi.ErrorCode=HAL_DCMI_ERROR_OVR;HAL_DCMI_ErrorCallback(&s_dcmi);}
}
static void SetRaw(uint32_t flags){*((volatile uint32_t *)&DCMI->RISR)=flags;}
static void DmaComplete(void)
{
 DMA1_Stream0->CR&=~DMA_SxCR_EN;DMA1_Stream0->NDTR=0;DMA1->LISR=DMA_FLAG_TCIF0_4;
 DMA1_Stream0_IRQHandler();
}
static void FrameComplete(void)
{
 DCMI->CR&=~DCMI_CR_CAPTURE;SetRaw(DCMI_FLAG_FRAMERI);DmaComplete();DCMI_IRQHandler();
}
static int Take(uint32_t flags,SysResult_t result)
{
 BspDcmiEvents_t event;
 EQ(BSP_DCMI_TakeEvents(&event),1);EQ(event.flags,flags);EQ(event.result,result);
 g_result.event_flags=event.flags;g_result.dcmi_error=event.dcmi_error;g_result.dma_error=event.dma_error;
 EQ(BSP_DCMI_TakeEvents(&event),0);return 0;
}
static int Stopped(void)
{
 EQ(DMA1_Stream0->CR&DMA_SxCR_EN,0);EQ(DCMI->CR&(DCMI_CR_CAPTURE|DCMI_CR_ENABLE),0);
 EQ(s_dcmi.State,HAL_DCMI_STATE_READY);EQ(s_dcmi_dma.State,HAL_DMA_STATE_READY);
 EQ(s_dcmi.Lock,HAL_UNLOCKED);EQ(s_dcmi_dma.Lock,HAL_UNLOCKED);EQ(s_stop_required,0);
 EQ(s_dcmi.DMA_Handle==&s_dcmi_dma,1);EQ(s_dcmi_dma.Parent==&s_dcmi,1);
 return 0;
}

static SysResult_t StreamCallback(BspDcmiTarget_t target,void *context)
{
 (void)target;(void)context;g_result.fail_line=__LINE__;return SYS_OK;
}
static int PrecheckCases(uint32_t test)
{
 uint32_t *m0=(uint32_t *)0xD0000400,*m1=(uint32_t *)0xD0009A00;
 uint32_t words=9600,i;SysResult_t expected=SYS_ERR_NOT_SUPPORTED,result;
 BspDcmiFrameCallback_t callback=StreamCallback;void *context=&s_stream;
 BspDcmiStream_t stream_before;DCMI_HandleTypeDef dcmi_before;DMA_HandleTypeDef dma_before;
 uint32_t saved[17],after[17];const uint8_t *a,*b;
 scenario=0;Seed();
 if(test!=16){EQ(Framework_InitRuntime(),SYS_OK);EQ(Config(),0);}
 switch(test){
 case 0:m0=NULL;expected=SYS_ERR_INVALID_ARG;break;
 case 1:m1=NULL;expected=SYS_ERR_INVALID_ARG;break;
 case 2:callback=NULL;expected=SYS_ERR_INVALID_ARG;break;
 case 3:words=0;expected=SYS_ERR_INVALID_ARG;break;
 case 4:words=65536;expected=SYS_ERR_INVALID_ARG;break;
 case 5:m0=(uint32_t *)0xD0000401;expected=SYS_ERR_INVALID_ARG;break;
 case 6:m1=(uint32_t *)0xD0009A02;expected=SYS_ERR_INVALID_ARG;break;
 case 7:m1=m0;expected=SYS_ERR_INVALID_ARG;break;
 case 8:m1=m0+1;expected=SYS_ERR_INVALID_ARG;break;
 case 9:m1=m0;m0=m1+1;expected=SYS_ERR_INVALID_ARG;break;
 case 10:m0=(uint32_t *)0xFFFFFFFC;words=2;expected=SYS_ERR_INVALID_ARG;break;
 case 11:m1=(uint32_t *)0xFFFFFFFC;words=2;expected=SYS_ERR_INVALID_ARG;break;
 case 12:m1=m0+words;break;
 case 13:words=65535;m1=(uint32_t *)0xD0100000;break;
 case 14:words=1;m0=(uint32_t *)0xFFFFFFFC;context=NULL;break;
 case 15:context=NULL;break;
 case 16:expected=SYS_ERR_INVALID_STATE;break;
 case 17:mock_ipsr=11;expected=SYS_ERR_INVALID_STATE;break;
 case 18:mock_primask=1;expected=SYS_ERR_INVALID_STATE;break;
 case 19:s_control_busy=true;expected=SYS_ERR_BUSY;break;
 case 20:s_capture_active=true;expected=SYS_ERR_BUSY;break;
 case 21:DMA1_Stream0->CR|=DMA_SxCR_EN;expected=SYS_ERR_BUSY;break;
 case 22:DCMI->CR|=DCMI_CR_CAPTURE;expected=SYS_ERR_BUSY;break;
 case 23:DCMI->CR|=DCMI_CR_ENABLE;expected=SYS_ERR_BUSY;break;
 case 24:s_dcmi.State=HAL_DCMI_STATE_BUSY;expected=SYS_ERR_BUSY;break;
 case 25:s_dcmi_dma.State=HAL_DMA_STATE_BUSY;expected=SYS_ERR_BUSY;break;
 case 26:s_dcmi.Lock=HAL_LOCKED;expected=SYS_ERR_BUSY;break;
 case 27:s_dcmi_dma.Lock=HAL_LOCKED;expected=SYS_ERR_BUSY;break;
 case 28:s_stop_required=true;expected=SYS_ERR_INVALID_STATE;break;
 case 29:s_stream.stop_required=true;expected=SYS_ERR_INVALID_STATE;break;
 case 30:s_stream.state=BSP_DCMI_STREAM_ERROR;expected=SYS_ERR_INVALID_STATE;break;
 case 31:s_stream.mode=BSP_DCMI_MODE_CONTINUOUS;expected=SYS_ERR_BUSY;break;
 case 32:s_stream.mode=BSP_DCMI_MODE_SNAPSHOT;expected=SYS_ERR_BUSY;break;
 case 33:s_stream.state=BSP_DCMI_STREAM_STARTING;expected=SYS_ERR_BUSY;break;
 case 34:s_stream.state=BSP_DCMI_STREAM_RUNNING;expected=SYS_ERR_BUSY;break;
 case 35:s_stop_required=true;s_control_busy=true;expected=SYS_ERR_INVALID_STATE;break;
 case 36:
  s_event_flags=BSP_DCMI_EVENT_FRAME;s_event_result=SYS_ERR_TIMEOUT;
  s_event_dcmi_error=0x80;s_event_dma_error=0x40;
  s_stream.frame_words=123;s_stream.callback=StreamCallback;s_stream.context=&s_dcmi;
  SetRaw(DCMI_FLAG_FRAMERI);DMA1->LISR=DMA_FLAG_TCIF0_4;break;
 case 37:m0=(uint32_t *)0xFFFFFFFC;words=1;m1=(uint32_t *)0xFFFFFFF8;break;
 case 38:m1=m0;m0=m1+words;break;
 case 39:words=4800;m1=m0+words;break;
 }
 stream_before=s_stream;dcmi_before=s_dcmi;dma_before=s_dcmi_dma;
 saved[0]=DCMI->CR;saved[1]=DCMI->IER;saved[2]=DCMI->RISR;
 saved[3]=DMA1_Stream0->CR;saved[4]=DMA1_Stream0->NDTR;saved[5]=DMA1_Stream0->PAR;
 saved[6]=DMA1_Stream0->M0AR;saved[7]=DMA1_Stream0->M1AR;saved[8]=DMA1->LISR;
 saved[9]=s_event_flags;saved[10]=(uint32_t)s_event_result;saved[11]=s_event_dcmi_error;saved[12]=s_event_dma_error;
 saved[13]=s_stop_required;saved[14]=s_control_busy;saved[15]=s_capture_active;saved[16]=mock_primask;
 result=BSP_DCMI_StartContinuous(m0,m1,words,callback,context);g_result.result=result;EQ(result,expected);
 after[0]=DCMI->CR;after[1]=DCMI->IER;after[2]=DCMI->RISR;
 after[3]=DMA1_Stream0->CR;after[4]=DMA1_Stream0->NDTR;after[5]=DMA1_Stream0->PAR;
 after[6]=DMA1_Stream0->M0AR;after[7]=DMA1_Stream0->M1AR;after[8]=DMA1->LISR;
 after[9]=s_event_flags;after[10]=(uint32_t)s_event_result;after[11]=s_event_dcmi_error;after[12]=s_event_dma_error;
 after[13]=s_stop_required;after[14]=s_control_busy;after[15]=s_capture_active;after[16]=mock_primask;
 for(i=0;i<17;i++)EQ(saved[i],after[i]);
 a=(const uint8_t *)&stream_before;b=(const uint8_t *)&s_stream;for(i=0;i<sizeof(s_stream);i++)EQ(a[i],b[i]);
 a=(const uint8_t *)&dcmi_before;b=(const uint8_t *)&s_dcmi;for(i=0;i<sizeof(s_dcmi);i++)EQ(a[i],b[i]);
 a=(const uint8_t *)&dma_before;b=(const uint8_t *)&s_dcmi_dma;for(i=0;i<sizeof(s_dcmi_dma);i++)EQ(a[i],b[i]);
 EQ(g_result.start_calls,0);EQ(g_result.stop_calls,0);return 0;
}

static int Cases(uint32_t id)
{
 uint32_t n;
 uint32_t *buffer=(uint32_t *)0xD0000000;
 BspDcmiEvents_t event;
 DCMI_HandleTypeDef other={0};
 scenario=0;Seed();
 if(id==0){EQ(BSP_DCMI_StartSnapshot(buffer,4800),SYS_ERR_INVALID_STATE);EQ(BSP_DCMI_Stop(),SYS_ERR_INVALID_STATE);EQ(BSP_DCMI_TakeEvents(&event),0);return 0;}
 EQ(Framework_InitRuntime(),SYS_OK);EQ(Config(),0);EQ(g_result.start_calls,0);
 if(id==1)return 0;
 if(id==2){
  EQ(BSP_DCMI_StartSnapshot(NULL,4800),SYS_ERR_INVALID_ARG);EQ(BSP_DCMI_StartSnapshot((uint32_t *)0xD0000001,4800),SYS_ERR_INVALID_ARG);
  EQ(BSP_DCMI_StartSnapshot(buffer,0),SYS_ERR_INVALID_ARG);EQ(BSP_DCMI_StartSnapshot(buffer,65536),SYS_ERR_INVALID_ARG);
  EQ(BSP_DCMI_StartSnapshot((uint32_t *)0xFFFFFFFC,2),SYS_ERR_INVALID_ARG);EQ(g_result.start_calls,0);return 0;
 }
 if(id==3){mock_ipsr=11;EQ(BSP_DCMI_StartSnapshot(buffer,1),SYS_ERR_INVALID_STATE);EQ(BSP_DCMI_Stop(),SYS_ERR_INVALID_STATE);return 0;}
 if(id==4){mock_primask=1;EQ(BSP_DCMI_StartSnapshot(buffer,1),SYS_ERR_INVALID_STATE);EQ(BSP_DCMI_Stop(),SYS_ERR_INVALID_STATE);EQ(mock_primask,1);return 0;}
 if(id==5){EQ(BSP_DCMI_Stop(),SYS_OK);EQ(Stopped(),0);return 0;}
 if(id==6){
  s_event_flags=BSP_DCMI_EVENT_ERROR;s_event_result=SYS_ERR_IO;s_event_dcmi_error=0x80;s_event_dma_error=0x40;
  s_dcmi.ErrorCode=0x80;s_dcmi_dma.ErrorCode=0x40;
  SetRaw(31);DMA1->LISR=63;pending[0]=pending[1]=1;
 }
 if(id>=18 && id<=20)fault_start=id-17;
 EQ(BSP_DCMI_StartSnapshot(buffer,4800),id>=18&&id<=20?(id==18?SYS_ERR_IO:id==19?SYS_ERR_BUSY:SYS_ERR_TIMEOUT):SYS_OK);
 if(id>=18 && id<=20){
  EQ(s_stop_required,1);EQ(DCMI->CR&(DCMI_CR_ENABLE|DCMI_CR_CAPTURE),0);
  EQ(Take(BSP_DCMI_EVENT_ERROR,id==18?SYS_ERR_IO:id==19?SYS_ERR_BUSY:SYS_ERR_TIMEOUT),0);
  fault_start=0;EQ(BSP_DCMI_Stop(),SYS_OK);EQ(Stopped(),0);EQ(BSP_DCMI_StartSnapshot(buffer,4800),SYS_OK);return 0;
 }
 EQ(DMA1_Stream0->NDTR,4800);EQ(DMA1_Stream0->PAR,(uint32_t)&DCMI->DR);EQ(DMA1_Stream0->M0AR,(uint32_t)buffer);
 EQ(DMA1_Stream0->CR&DMA_SxCR_EN,DMA_SxCR_EN);EQ(DCMI->CR&(DCMI_CR_ENABLE|DCMI_CR_CAPTURE),DCMI_CR_ENABLE|DCMI_CR_CAPTURE);
 EQ(DCMI->IER&(DCMI_IT_ERR|DCMI_IT_OVR),DCMI_IT_ERR|DCMI_IT_OVR);EQ(DCMI->IER&DCMI_IT_FRAME,0);EQ(mock_primask,0);
 if(id==6){EQ(s_event_flags,0);EQ(s_dcmi.ErrorCode,0);EQ(s_dcmi_dma.ErrorCode,0);EQ(DCMI->RISR,0);EQ(DMA1->LISR,0);EQ(BSP_DCMI_TakeEvents(&event),0);return 0;}
 if(id==7){n=g_result.start_calls;EQ(BSP_DCMI_StartSnapshot(buffer,4800),SYS_ERR_BUSY);EQ(g_result.start_calls,n);return 0;}
 if(id==8){DmaComplete();EQ(s_dcmi.State,HAL_DCMI_STATE_READY);EQ(BSP_DCMI_TakeEvents(&event),0);EQ(BSP_DCMI_StartSnapshot(buffer,4800),SYS_ERR_BUSY);return 0;}
 if(id==9){FrameComplete();EQ(Take(BSP_DCMI_EVENT_FRAME,SYS_OK),0);return 0;}
 if(id==10){FrameComplete();EQ(DCMI->IER&(DCMI_IT_ERR|DCMI_IT_OVR),0);EQ(Take(BSP_DCMI_EVENT_FRAME,SYS_OK),0);EQ(BSP_DCMI_StartSnapshot(buffer,4800),SYS_OK);FrameComplete();EQ(Take(BSP_DCMI_EVENT_FRAME,SYS_OK),0);return 0;}
 if(id==11 || id==12){
  SetRaw(id==11?DCMI_FLAG_ERRRI:DCMI_FLAG_OVRRI);DCMI_IRQHandler();
  EQ(s_dcmi_dma.State,HAL_DMA_STATE_ABORT);EQ(Take(BSP_DCMI_EVENT_ERROR,SYS_ERR_IO),0);
  EQ(BSP_DCMI_StartSnapshot(buffer,4800),SYS_ERR_INVALID_STATE);
  EQ(BSP_DCMI_Stop(),SYS_OK);EQ(Stopped(),0);EQ(BSP_DCMI_StartSnapshot(buffer,4800),SYS_OK);return 0;
 }
 if(id==13){FrameComplete();/* completion not yet consumed */SetRaw(DCMI_FLAG_ERRRI|DCMI_FLAG_FRAMERI);DCMI_IRQHandler();EQ(Take(BSP_DCMI_EVENT_ERROR,SYS_ERR_IO),0);EQ(g_result.dcmi_error&HAL_DCMI_ERROR_SYNC,HAL_DCMI_ERROR_SYNC);return 0;}
 if(id==14){DMA1->LISR=DMA_FLAG_TEIF0_4;DMA1_Stream0_IRQHandler();EQ(Take(BSP_DCMI_EVENT_ERROR,SYS_ERR_IO),0);EQ(g_result.dma_error&HAL_DMA_ERROR_TE,HAL_DMA_ERROR_TE);EQ(BSP_DCMI_Stop(),SYS_OK);EQ(Stopped(),0);return 0;}
 if(id==15){s_dcmi_dma.ErrorCode=HAL_DMA_ERROR_FE;HAL_DCMI_ErrorCallback(&s_dcmi);EQ(Take(BSP_DCMI_EVENT_ERROR,SYS_ERR_IO),0);EQ(g_result.dma_error,HAL_DMA_ERROR_FE);return 0;}
 if(id==16){HAL_DCMI_FrameEventCallback(&other);HAL_DCMI_ErrorCallback(&other);EQ(BSP_DCMI_TakeEvents(&event),0);return 0;}
 if(id==17){HAL_DCMI_FrameEventCallback(&s_dcmi);EQ(Take(BSP_DCMI_EVENT_ERROR,SYS_ERR_PROTOCOL),0);return 0;}
 if(id==21){SetRaw(DCMI_FLAG_ERRRI);DCMI_IRQHandler();/* no abort IRQ delivered */EQ(BSP_DCMI_Stop(),SYS_OK);EQ(Stopped(),0);EQ(Take(BSP_DCMI_EVENT_ERROR,SYS_ERR_IO),0);return 0;}
 if(id==22){s_dcmi_dma.State=HAL_DMA_STATE_READY;/* stale HAL state while DMA EN=1 */fault_stop=1;EQ(BSP_DCMI_Stop(),SYS_OK);EQ(Stopped(),0);return 0;}
 if(id==23 || id==24){fault_stop=id==23?2:3;EQ(BSP_DCMI_Stop(),id==23?SYS_ERR_TIMEOUT:SYS_ERR_IO);EQ(Stopped(),0);EQ(BSP_DCMI_StartSnapshot(buffer,4800),SYS_OK);return 0;}
 if(id==25){
  g_stuck_stream=1;EQ(BSP_DCMI_Stop(),SYS_ERR_TIMEOUT);EQ(s_stop_required,1);EQ(g_result.nvic_enabled,0);
  EQ(BSP_DCMI_StartSnapshot(buffer,4800),SYS_ERR_INVALID_STATE);EQ(Take(BSP_DCMI_EVENT_ERROR,SYS_ERR_TIMEOUT),0);
  g_stuck_stream=0;EQ(BSP_DCMI_Stop(),SYS_OK);EQ(Stopped(),0);return 0;
 }
 if(id>=26 && id<=29){
  if(id==26)fault_dma_deinit=1;if(id==27)fault_dma_init=1;if(id==28)fault_dcmi_deinit=1;if(id==29)fault_dcmi_init=1;
  EQ(BSP_DCMI_Stop(),SYS_ERR_IO);EQ(g_result.nvic_enabled,0);EQ(s_stop_required,1);
  fault_dma_deinit=fault_dma_init=fault_dcmi_deinit=fault_dcmi_init=0;EQ(BSP_DCMI_Stop(),SYS_OK);EQ(Stopped(),0);return 0;
 }
 if(id==30){s_dcmi_dma.State=HAL_DMA_STATE_ABORT;s_dcmi_dma.Lock=HAL_LOCKED;EQ(BSP_DCMI_Stop(),SYS_OK);EQ(Stopped(),0);return 0;}
 if(id==31){EQ(BSP_DCMI_Stop(),SYS_OK);HAL_DCMI_FrameEventCallback(&s_dcmi);s_dcmi.ErrorCode=HAL_DCMI_ERROR_OVR;HAL_DCMI_ErrorCallback(&s_dcmi);EQ(BSP_DCMI_TakeEvents(&event),0);return 0;}
 if(id==32){FrameComplete();EQ(BSP_DCMI_TakeEvents(NULL),0);EQ(Take(BSP_DCMI_EVENT_FRAME,SYS_OK),0);return 0;}
 if(id==33){arm_deferred=1;EQ(BSP_DCMI_TakeEvents(&event),0);EQ(Take(BSP_DCMI_EVENT_FRAME,SYS_OK),0);EQ(mock_primask,0);return 0;}
 if(id==34){FrameComplete();arm_deferred=2;EQ(BSP_DCMI_TakeEvents(&event),1);EQ(event.flags,BSP_DCMI_EVENT_FRAME);EQ(Take(BSP_DCMI_EVENT_ERROR,SYS_ERR_IO),0);return 0;}
 if(id==35){
  FrameComplete();mock_primask=1;arm_deferred=2;EQ(BSP_DCMI_TakeEvents(&event),1);EQ(event.flags,BSP_DCMI_EVENT_FRAME);EQ(mock_primask,1);EQ(deferred,2);
  Test_SetPrimask(0);EQ(Take(BSP_DCMI_EVENT_ERROR,SYS_ERR_IO),0);return 0;
 }
 if(id==36){FrameComplete();SetRaw(DCMI_FLAG_OVRRI);EQ(Take(BSP_DCMI_EVENT_ERROR,SYS_ERR_IO),0);EQ(g_result.dcmi_error&HAL_DCMI_ERROR_OVR,HAL_DCMI_ERROR_OVR);return 0;}
 if(id==37){FrameComplete();EQ(BSP_DCMI_Stop(),SYS_OK);EQ(Take(BSP_DCMI_EVENT_FRAME,SYS_OK),0);return 0;}
 if(id==38){s_control_busy=true;EQ(BSP_DCMI_Stop(),SYS_ERR_BUSY);EQ(BSP_DCMI_StartSnapshot(buffer,4800),SYS_ERR_BUSY);s_control_busy=false;return 0;}
 if(id==39){SetRaw(DCMI_FLAG_ERRRI);DCMI_IRQHandler();DMA1->LISR=DMA_FLAG_TCIF0_4;DMA1_Stream0_IRQHandler();EQ(Take(BSP_DCMI_EVENT_ERROR,SYS_ERR_IO),0);EQ(BSP_DCMI_Stop(),SYS_OK);EQ(Stopped(),0);return 0;}
 if(id==40){FrameComplete();EQ(Take(BSP_DCMI_EVENT_FRAME,SYS_OK),0);s_dcmi.Lock=HAL_LOCKED;EQ(BSP_DCMI_StartSnapshot(buffer,4800),SYS_ERR_BUSY);EQ(BSP_DCMI_Stop(),SYS_ERR_BUSY);EQ(Stopped(),0);return 0;}
 if(id==41){tick=0xFFFFFFF0U;g_stuck_stream=1;EQ(BSP_DCMI_Stop(),SYS_ERR_TIMEOUT);EQ(s_stop_required,1);return 0;}
 if(id==42){FrameComplete();EQ(BSP_DCMI_StartSnapshot(buffer,4800),SYS_OK);EQ(BSP_DCMI_TakeEvents(&event),0);return 0;}
 if(id==43){SetRaw(DCMI_FLAG_ERRRI);DCMI_IRQHandler();EQ(Take(BSP_DCMI_EVENT_ERROR,SYS_ERR_IO),0);HAL_DCMI_FrameEventCallback(&s_dcmi);EQ(BSP_DCMI_TakeEvents(&event),0);return 0;}
 if(id==44){FrameComplete();EQ(BSP_DCMI_StartSnapshot(NULL,4800),SYS_ERR_INVALID_ARG);EQ(Take(BSP_DCMI_EVENT_FRAME,SYS_OK),0);return 0;}
 if(id==45){EQ(BSP_DCMI_Stop(),SYS_OK);EQ(BSP_DCMI_TakeEvents(&event),0);EQ(BSP_DCMI_Stop(),SYS_OK);EQ(BSP_DCMI_TakeEvents(&event),0);return 0;}
 return 0;
}
int Dcmi_TestRun(uint32_t id)
{
 int result=id<46?Cases(id):PrecheckCases(id-46);
 g_result.dma_cr=DMA1_Stream0->CR;g_result.dcmi_cr=DCMI->CR;g_result.request=s_dcmi_dma.Init.Request;g_result.ready=s_dcmi_ready;
 g_result.stop_required=s_stop_required;g_result.primask=mock_primask;g_result.active=s_capture_active;
 return result;
}
