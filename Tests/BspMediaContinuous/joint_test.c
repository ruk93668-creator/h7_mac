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
static HAL_StatusTypeDef Test_ChangeMemory(DMA_HandleTypeDef *h,uint32_t address,HAL_DMA_MemoryTypeDef memory);
#define HAL_DMAEx_ChangeMemory Test_ChangeMemory
static HAL_StatusTypeDef Test_DBMStart(DMA_HandleTypeDef *,uint32_t,uint32_t,uint32_t,uint32_t);
#define HAL_DMAEx_MultiBufferStart_IT Test_DBMStart
#include "../../CM7/BSP/bsp_dcmi.c"
#undef HAL_DMAEx_MultiBufferStart_IT
#undef HAL_DMAEx_ChangeMemory
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
#define __get_PRIMASK Test_Primask
#define __get_IPSR Test_Ipsr
#define __disable_irq Test_DisableIrq
#define __set_PRIMASK Test_SetPrimask
#include "../../CM7/Media/Src/camera_capture.c"
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
static void Service(void);
volatile uint32_t g_inject_start,g_queued_start;
volatile uint32_t g_ref_tc,g_ref_frame,g_ref_delivered;
static uint32_t tc_queued,frame_queued,prefer_frame,dbm_fault,replace_fault,replace_calls,dbm_checks,sample;
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
static void Test_SetPrimask(uint32_t value){mock_primask=value;if(!value && deferred)DeliverDeferred();if(!value)Service();}
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
static HAL_StatusTypeDef Test_DBMStart(DMA_HandleTypeDef *h,uint32_t src,uint32_t m0,uint32_t m1,uint32_t n)
{
 dbm_checks++;
 if(mock_primask!=1 || !s_control_busy || s_accept_events || s_capture_active ||
    s_event_flags || s_event_result!=SYS_OK || s_event_dcmi_error || s_event_dma_error ||
    s_dcmi.ErrorCode || s_dcmi_dma.ErrorCode || DCMI->RISR || DMA1->LISR || pending[0] || pending[1])g_result.fail_line=__LINE__;
 if(dbm_fault)return dbm_fault==1?HAL_ERROR:dbm_fault==2?HAL_BUSY:HAL_TIMEOUT;
 return HAL_DMAEx_MultiBufferStart_IT(h,src,m0,m1,n);
}
static HAL_StatusTypeDef Test_ChangeMemory(DMA_HandleTypeDef *h,uint32_t address,HAL_DMA_MemoryTypeDef memory)
{
 HAL_StatusTypeDef result;
 replace_calls++;
 if(replace_fault==1)return HAL_ERROR;
 result=HAL_DMAEx_ChangeMemory(h,address,memory);
 if(replace_fault==2)DMA1_Stream0->CR^=DMA_SxCR_CT;
 return result;
}
static int Valid(void)
{
 uint32_t i,dma=0;CameraStream_t *me=&s_capture.stream;
 EQ(me->dma_m0_index<3,1);EQ(me->dma_m1_index<3,1);EQ(me->dma_m0_index!=me->dma_m1_index,1);
 EQ(DMA1_Stream0->M0AR,me->buffers[me->dma_m0_index].data);EQ(DMA1_Stream0->M1AR,me->buffers[me->dma_m1_index].data);
 for(i=0;i<3;i++){
  if(i==me->dma_m0_index || i==me->dma_m1_index){EQ(me->buffers[i].state,CAMERA_BUFFER_DMA);dma++;}
  else if(me->buffers[i].state==CAMERA_BUFFER_READY || me->buffers[i].state==CAMERA_BUFFER_IN_USE)EQ(me->consumer_index,i);
  else{EQ(me->buffers[i].state,CAMERA_BUFFER_FREE);EQ(me->consumer_index,CAMERA_BUFFER_INDEX_INVALID);}
 }
 EQ(dma,2);return 0;
}
//模拟DMA写入当前目标的首尾字节，并切换CT、重装NDTR及置TC标志
static void HardwareTc(void)
{
 uint32_t active=(DMA1_Stream0->CR&DMA_SxCR_CT)?1U:0U;
 uint32_t address=active?DMA1_Stream0->M1AR:DMA1_Stream0->M0AR;
 uint32_t bytes=s_stream.frame_words*4U;
 sample++;g_ref_tc=sample;
 *(volatile uint8_t *)address=(uint8_t)sample;
 *(volatile uint8_t *)(address+bytes-1U)=(uint8_t)sample;
 DMA1_Stream0->CR^=DMA_SxCR_CT;DMA1_Stream0->NDTR=s_stream.frame_words-64U;
 DMA1->LISR|=DMA_FLAG_TCIF0_4;tc_queued=1;
}
static void Service(void)
{
 if(mock_primask || mock_ipsr || !s_dcmi_ready)return;
 if(g_queued_start){
  g_queued_start=0;
  if(s_stream.state!=BSP_DCMI_STREAM_RUNNING || !s_capture_active || s_control_busy || (DCMI->CR&DCMI_CR_CAPTURE)==0U)g_result.fail_line=__LINE__;
  HardwareTc();SetRaw(DCMI_FLAG_FRAMERI);frame_queued=1;
 }
 if(prefer_frame && frame_queued && (g_result.nvic_enabled&2)){frame_queued=0;mock_ipsr=94;DCMI_IRQHandler();mock_ipsr=0;}
 if(tc_queued && (g_result.nvic_enabled&1)){tc_queued=0;mock_ipsr=27;DMA1_Stream0_IRQHandler();mock_ipsr=0;}
 if(frame_queued && (g_result.nvic_enabled&2)){frame_queued=0;mock_ipsr=94;DCMI_IRQHandler();mock_ipsr=0;}
}
static void FullFrame(uint32_t frame_first)
{
 prefer_frame=frame_first;
 if(frame_first){SetRaw(DCMI_FLAG_FRAMERI);frame_queued=1;Service();HardwareTc();Service();}
 else{HardwareTc();Service();SetRaw(DCMI_FLAG_FRAMERI);frame_queued=1;Service();}
}
static int Begin(uint32_t format)
{
 CameraConfig_t config={160,120,(CameraPixelFormat_t)format};
 Seed();EQ(Framework_InitRuntime(),SYS_OK);EQ(Config(),0);
 EQ(CameraCapture_Init(&config),SYS_OK);return 0;
}
static int Start(void)
{
 EQ(CameraCapture_StartStream(),SYS_OK);
 EQ(s_stream.callback==CameraStream_OnFrameComplete,1);EQ(s_stream.context,&s_capture.stream);
 EQ(s_stream.frame_words,s_capture.stream.frame_bytes/4);EQ(s_stream.state,BSP_DCMI_STREAM_RUNNING);
 EQ(s_capture.state,CAMERA_CAPTURE_CAPTURING);EQ(mock_primask,0);EQ(s_control_busy,0);
 EQ((DMA1_Stream0->CR&(DMA_SxCR_EN|DMA_SxCR_DBM)),DMA_SxCR_EN|DMA_SxCR_DBM);
 EQ(DMA1_Stream0->NDTR,s_stream.frame_words);EQ(Valid(),0);return 0;
}
static int JointCases(uint32_t id)
{
 uint32_t kind=id/3,fmt=id%3,i,n,held,prior;CameraFrame_t frame={0};BspDcmiEvents_t event;CameraStream_t before;
 EQ(Begin(fmt),0);
 if(kind==14 || kind==15 || kind==16 || kind==21){
  s_event_flags=BSP_DCMI_EVENT_ERROR;s_event_result=SYS_ERR_TIMEOUT;s_event_dcmi_error=0x80;s_event_dma_error=0x40;
  if(kind==14){EQ(BSP_DCMI_StartContinuous(NULL,(uint32_t *)0xD000A000,4800,CameraStream_OnFrameComplete,&s_capture.stream),SYS_ERR_INVALID_ARG);EQ(s_event_flags,BSP_DCMI_EVENT_ERROR);EQ(s_event_result,SYS_ERR_TIMEOUT);EQ(s_event_dcmi_error,0x80);EQ(s_event_dma_error,0x40);return 0;}
  if(kind==15){s_control_busy=true;EQ(CameraCapture_StartStream(),SYS_ERR_BUSY);EQ(s_event_flags,BSP_DCMI_EVENT_ERROR);EQ(s_event_result,SYS_ERR_TIMEOUT);EQ(s_event_dcmi_error,0x80);EQ(s_event_dma_error,0x40);return 0;}
  if(kind==21){EQ(BSP_DCMI_Stop(),SYS_OK);EQ(Take(BSP_DCMI_EVENT_ERROR,SYS_ERR_TIMEOUT),0);EQ(g_result.dcmi_error,0x80);EQ(g_result.dma_error,0x40);return 0;}
  s_dcmi.ErrorCode=HAL_DCMI_ERROR_SYNC;s_dcmi_dma.ErrorCode=HAL_DMA_ERROR_TE;
  SetRaw(DCMI_FLAG_FRAMERI|DCMI_FLAG_OVRRI|DCMI_FLAG_ERRRI);DMA1->LISR=DMA_FLAG_TCIF0_4|DMA_FLAG_TEIF0_4;pending[0]=pending[1]=1;
 }
 if(kind==32 || kind==33){
  EQ(Start(),0);Test_DisableIrq();HardwareTc();g_ref_frame=sample;SetRaw(DCMI_FLAG_FRAMERI);frame_queued=1;
  HardwareTc();HardwareTc();prefer_frame=kind==32;Test_SetPrimask(0);
  EQ(s_stop_required,0);EQ(s_frame_sync.paired_frames,1);EQ(CameraCapture_StreamGetFrame(&frame),true);
  g_ref_delivered=frame.data[0];EQ(g_ref_tc,3);EQ(g_ref_frame,1);EQ(g_ref_delivered,3);EQ(Valid(),0);return 0;
 }
 if(kind==35){
  EQ(Start(),0);HardwareTc();Service();HardwareTc();HardwareTc();SetRaw(DCMI_FLAG_FRAMERI);frame_queued=1;prefer_frame=1;Service();
  EQ(s_stop_required,1);EQ(s_frame_sync.paired_frames,0);return 0;
 }
 if(kind==17)g_inject_start=1;
 if(kind==18){dbm_fault=1;EQ(CameraCapture_StartStream(),SYS_ERR_IO);EQ(s_capture.state,CAMERA_CAPTURE_ERROR);EQ(s_event_result,SYS_ERR_IO);EQ(s_stop_required,1);dbm_fault=0;EQ(CameraCapture_Stop(),SYS_OK);EQ(BSP_DCMI_TakeEvents(&event),true);EQ(event.result,SYS_ERR_IO);return 0;}
 if(kind==19 || kind==20){
  EQ(CameraCapture_Start(),SYS_OK);
  if(kind==19){FrameComplete();CameraCapture_Process();EQ(CameraCapture_GetFrame(&frame),true);EQ(CameraCapture_ReleaseFrame(),SYS_OK);EQ(s_capture.mode,CAMERA_CAPTURE_MODE_NONE);EQ(Start(),0);return 0;}
  tick=s_capture.snapshot.start_tick+100;CameraCapture_Process();EQ(s_capture.mode,CAMERA_CAPTURE_MODE_NONE);EQ(CameraCapture_GetLastResult(),SYS_ERR_TIMEOUT);return 0;
 }
 if(kind==17){EQ(CameraCapture_StartStream(),SYS_OK);EQ(s_frame_sync.paired_frames,1);EQ(s_stop_required,0);EQ(s_capture.stream.next_sequence,1);EQ(Valid(),0);return 0;}
 EQ(Start(),0);
 if(kind==0){EQ(dbm_checks,1);return 0;}
 if(kind>=1 && kind<=4){
  if(kind>=3)FullFrame(0);
  before=s_capture.stream;n=replace_calls;
  FullFrame(kind==2 || kind==4);
  EQ(replace_calls,n+1);EQ(s_capture.stream.next_sequence,before.next_sequence+1);EQ(s_frame_sync.paired_frames,kind>=3?2:1);EQ(s_frame_sync.tc_pending,0);EQ(s_frame_sync.frame_pending,0);EQ(Valid(),0);
  EQ(CameraCapture_StreamGetFrame(&frame),true);EQ(frame.size_bytes,s_capture.stream.frame_bytes);EQ(frame.format,fmt);EQ(frame.data[0],sample);
  EQ(CameraCapture_StreamReleaseFrame(),SYS_OK);EQ(Valid(),0);return 0;
 }
 if(kind==5 || kind==6 || kind==22){
  for(i=0;i<(kind==6?30U:12U);i++){
   FullFrame(i&1);EQ(Valid(),0);
   if(kind!=6){EQ(CameraCapture_StreamGetFrame(&frame),true);EQ(frame.data[0],sample);EQ(CameraCapture_StreamReleaseFrame(),SYS_OK);}
  }
  EQ(s_capture.stream.dropped_frames,kind==6?29:0);
  if(kind==22){EQ(CameraCapture_Stop(),SYS_OK);EQ(Start(),0);FullFrame(1);EQ(s_frame_sync.paired_frames,1);EQ(Valid(),0);}
  return 0;
 }
 if(kind==9 || kind==10 || kind==24 || kind==25 || kind==26 || kind==31){
  before=s_capture.stream;
  if(kind==9)replace_fault=1;
  if(kind==10)replace_fault=2;
  if(kind==31)s_capture.stream.buffers[2].data=s_capture.stream.buffers[0].data;
  if(kind==24){HardwareTc();Service();DMA1_Stream0->NDTR=32;SetRaw(DCMI_FLAG_FRAMERI);frame_queued=1;Service();}
  else if(kind==25){HardwareTc();Service();HardwareTc();Service();}
  else if(kind==26){SetRaw(DCMI_FLAG_FRAMERI);frame_queued=1;Service();SetRaw(DCMI_FLAG_FRAMERI);frame_queued=1;Service();}
  else FullFrame(0);
  EQ(s_stop_required,1);EQ(s_frame_sync.paired_frames,0);EQ(s_capture.stream.next_sequence,0);
  EQ(s_capture.stream.dma_m0_index,before.dma_m0_index);EQ(s_capture.stream.dma_m1_index,before.dma_m1_index);
  for(i=0;i<3;i++)EQ(s_capture.stream.buffers[i].state,before.buffers[i].state);
  CameraCapture_Process();EQ(s_capture.mode,CAMERA_CAPTURE_MODE_NONE);EQ(s_capture.state,CAMERA_CAPTURE_IDLE);EQ(g_result.stop_calls,1);return 0;
 }
 if(kind==13){
  SetRaw(DCMI_FLAG_OVRRI);DCMI_IRQHandler();EQ(s_event_flags,BSP_DCMI_EVENT_ERROR);
  EQ(CameraCapture_Stop(),SYS_OK);EQ(s_event_flags,BSP_DCMI_EVENT_ERROR);EQ(s_event_result,SYS_ERR_IO);
  EQ(Start(),0);CameraCapture_Process();EQ(s_capture.state,CAMERA_CAPTURE_CAPTURING);EQ(s_capture.last_result,SYS_OK);EQ(s_event_flags,0);return 0;
 }
 if(kind==16){EQ(s_event_flags,0);EQ(s_event_result,SYS_OK);EQ(s_event_dcmi_error,0);EQ(s_event_dma_error,0);EQ(DCMI->RISR,0);EQ(DMA1->LISR,0);CameraCapture_Process();EQ(s_capture.state,CAMERA_CAPTURE_CAPTURING);return 0;}
 FullFrame(0);EQ(CameraCapture_StreamGetFrame(&frame),true);held=frame.data[0];prior=replace_calls;
 if(kind==7){for(i=0;i<100;i++){FullFrame(i&1);EQ(Valid(),0);EQ(frame.data[0],held);EQ(frame.data[frame.size_bytes-1],held);}EQ(replace_calls,prior);EQ(s_capture.stream.dropped_frames,100);EQ(CameraCapture_StreamReleaseFrame(),SYS_OK);FullFrame(0);EQ(Valid(),0);return 0;}
 if(kind==8){EQ(CameraCapture_Stop(),SYS_ERR_BUSY);EQ(g_result.stop_calls,0);EQ(DMA1_Stream0->CR&DMA_SxCR_EN,DMA_SxCR_EN);EQ(CameraCapture_StreamReleaseFrame(),SYS_OK);EQ(CameraCapture_Stop(),SYS_OK);EQ(s_capture.mode,CAMERA_CAPTURE_MODE_NONE);return 0;}
 if(kind==11 || kind==12 || kind==30){
  SetRaw(DCMI_FLAG_OVRRI);DCMI_IRQHandler();
  if(kind==12)fault_stop=2;
  CameraCapture_Process();EQ(s_capture.last_result,SYS_ERR_IO);EQ(frame.data[0],held);
  if(kind==12){EQ(s_capture.state,CAMERA_CAPTURE_ERROR);fault_stop=0;CameraCapture_Process();}
  EQ(s_capture.state,CAMERA_CAPTURE_DRAINING);EQ(s_capture.mode,CAMERA_CAPTURE_MODE_STREAM);
  EQ(CameraCapture_StreamGetFrame(&frame),false);EQ(CameraCapture_StartStream(),SYS_ERR_BUSY);EQ(CameraCapture_Stop(),SYS_ERR_BUSY);
  EQ(CameraCapture_StreamReleaseFrame(),SYS_OK);EQ(s_capture.mode,CAMERA_CAPTURE_MODE_NONE);EQ(Start(),0);return 0;
 }
 if(kind==23){
  EQ(CameraCapture_StreamReleaseFrame(),SYS_OK);FullFrame(0);Test_DisableIrq();HardwareTc();SetRaw(DCMI_FLAG_FRAMERI);frame_queued=1;
  n=s_frame_sync.paired_frames;EQ(CameraCapture_StreamGetFrame(&frame),true);EQ(mock_primask,1);EQ(s_frame_sync.paired_frames,n);
  Test_SetPrimask(0);EQ(s_frame_sync.paired_frames,n+1);EQ(frame.data[0],sample-1);EQ(Valid(),0);return 0;
 }
 //以下为风险反例：消费者持有时不调用地址替换，CT与双pending并不提供帧号
 if(kind==34){
  HardwareTc();Service();HardwareTc();HardwareTc();SetRaw(DCMI_FLAG_FRAMERI);frame_queued=1;prefer_frame=1;Service();
  EQ(s_stop_required,1);EQ(frame.data[0],held);return 0;
 }
 if(kind==27){
  HardwareTc();Service();HardwareTc();SetRaw(DCMI_FLAG_FRAMERI);frame_queued=1;prefer_frame=1;Service();
  EQ(s_stop_required,1);return 0;
 }
 if(kind==28 || kind==29){
  //屏蔽普通IRQ期间跨过三个DMA目标完成，硬件TC位合并，CT仅保留奇偶
  Test_DisableIrq();HardwareTc();g_ref_frame=sample;SetRaw(DCMI_FLAG_FRAMERI);frame_queued=1;HardwareTc();HardwareTc();
  prefer_frame=kind==28;Test_SetPrimask(0);
  EQ(s_stop_required,0);EQ(s_frame_sync.paired_frames,2);EQ(s_capture.stream.dropped_frames,1);EQ(frame.data[0],held);return 0;
 }
 EQ(0,1);return 1;
}

//记录最终硬件寄存器与Media所有权，便于逐项审查
volatile uint32_t g_joint_view[20];
int Dcmi_TestRun(uint32_t id)
{
 int result=JointCases(id);uint32_t i;
 g_result.dma_cr=DMA1_Stream0->CR;g_result.dcmi_cr=DCMI->CR;g_result.request=s_dcmi_dma.Init.Request;g_result.ready=s_dcmi_ready;
 g_result.stop_required=s_stop_required;g_result.primask=mock_primask;g_result.active=s_capture_active;
 g_joint_view[0]=s_capture.mode;g_joint_view[1]=s_capture.state;g_joint_view[2]=(uint32_t)s_capture.last_result;
 g_joint_view[3]=s_capture.stream.dma_m0_index;g_joint_view[4]=s_capture.stream.dma_m1_index;g_joint_view[5]=s_capture.stream.consumer_index;
 g_joint_view[6]=s_capture.stream.next_sequence;g_joint_view[7]=s_capture.stream.dropped_frames;g_joint_view[8]=s_capture.stream.frame_bytes;
 for(i=0;i<3;i++)g_joint_view[9+i]=s_capture.stream.buffers[i].state;
 g_joint_view[12]=s_frame_sync.paired_frames;g_joint_view[13]=s_frame_sync.tc_pending;g_joint_view[14]=s_frame_sync.frame_pending;
 g_joint_view[15]=s_stream.state;g_joint_view[16]=s_stop_required;g_joint_view[17]=sample;g_joint_view[18]=g_ref_frame;g_joint_view[19]=g_ref_tc;
 return result;
}
