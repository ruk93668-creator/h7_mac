#include "stm32h7xx_hal.h"
static HAL_StatusTypeDef Test_DMA_Init(DMA_HandleTypeDef *h);
static HAL_StatusTypeDef Test_DCMI_Init(DCMI_HandleTypeDef *h);
static HAL_StatusTypeDef Test_DMA_DeInit(DMA_HandleTypeDef *h);
#define HAL_DMA_Init Test_DMA_Init
#define HAL_DCMI_Init Test_DCMI_Init
#define HAL_DMA_DeInit Test_DMA_DeInit
#include "../../../CM7/BSP/bsp_dcmi.c"
#undef HAL_DMA_Init
#undef HAL_DCMI_Init
#undef HAL_DMA_DeInit
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
 uint32_t fail_line,checks,dma_init,dcmi_init,dma_deinit,nvic_enabled,nvic_cleared,dma_callback,dcmi_callback;
 uint32_t dma_cr,dcmi_cr,request,ready;
 int32_t result;
} TestResult;
volatile TestResult g_result;
uint32_t SystemCoreClock=480000000;
static uint32_t scenario,inject,pending[2],priority[2],subpriority[2],tick;
static int Check(uint32_t line,int32_t a,int32_t b){g_result.checks++;if(a!=b){if(!g_result.fail_line)g_result.fail_line=line;return 0;}return 1;}
#define EQ(a,b) do{if(!Check(__LINE__,(int32_t)(a),(int32_t)(b)))return 1;}while(0)
uint32_t HAL_GetTick(void){return tick++;}
static HAL_StatusTypeDef Test_DMA_Init(DMA_HandleTypeDef *h)
{
 g_result.dma_init++;
 if(h!=&s_dcmi_dma)g_result.fail_line=__LINE__;
 if(inject==6 || inject==13)return HAL_ERROR;
 if(inject==7)return HAL_BUSY;
 if(inject==8)return HAL_TIMEOUT;
 return HAL_DMA_Init(h);
}
static HAL_StatusTypeDef Test_DCMI_Init(DCMI_HandleTypeDef *h)
{
 g_result.dcmi_init++;
 if(h!=&s_dcmi || h->DMA_Handle!=&s_dcmi_dma || s_dcmi_dma.Parent!=h)g_result.fail_line=__LINE__;
 if(inject==9 || inject==12 || inject==14)return HAL_ERROR;
 if(inject==10)return HAL_BUSY;
 if(inject==11)return HAL_TIMEOUT;
 return HAL_DCMI_Init(h);
}
static HAL_StatusTypeDef Test_DMA_DeInit(DMA_HandleTypeDef *h)
{
 g_result.dma_deinit++;
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
static void Test_DmaComplete(DMA_HandleTypeDef *h){if(h!=&s_dcmi_dma || h->Parent!=&s_dcmi)g_result.fail_line=__LINE__;g_result.dma_callback++;}

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
static int Cases(uint32_t id)
{
 uint32_t gpio_b_moder,gpio_b_af;
 SysResult_t result,expected;
 DCMI_HandleTypeDef other={0};
 scenario=id;Seed();
 if(id==16){HAL_DCMI_MspInit(&other);EQ(s_dcmi_pins_ready,0);EQ(RCC->AHB1ENR,0);return 0;}
 if(id!=2)BSP_DCMI_EarlyInit();
 if(id==1){gpio_b_moder=GPIOB->MODER;gpio_b_af=GPIOB->AFR[1];BSP_DCMI_EarlyInit();EQ(GPIOB->MODER,gpio_b_moder);EQ(GPIOB->AFR[1],gpio_b_af);}
 if(id==3)DMA1_Stream0->CR=DMA_SxCR_EN;
 if(id==4)DCMI->CR=DCMI_CR_ENABLE;
 if(id==5)DCMI->CR=DCMI_CR_CAPTURE;
 result=Framework_InitRuntime();g_result.result=result;
 if(id>=3 && id<=12){
  expected=(id==3||id==4||id==5||id==7||id==10)?SYS_ERR_BUSY:(id==8||id==11)?SYS_ERR_TIMEOUT:SYS_ERR_IO;
  EQ(result,expected);EQ(s_dcmi_ready,0);EQ(g_result.nvic_enabled,0);
  if(id<=5){EQ(g_result.dma_init,0);EQ(g_result.dcmi_init,0);}
  if(id>=6 && id<=8)EQ(g_result.dcmi_init,0);
  if(id>=9)EQ(g_result.dma_deinit,1);
  return 0;
 }
 if(id==13 || id==14){EQ(result,SYS_ERR_IO);EQ(g_result.nvic_enabled,0);inject=0;EQ(BSP_DCMI_Init(),SYS_OK);}
 else EQ(result,SYS_OK);
 EQ(Config(),0);
 if(id==1){EQ(BSP_DCMI_Init(),SYS_OK);EQ(g_result.dma_init,1);EQ(g_result.dcmi_init,1);}
 if(id==15){
  //仅注入IRQ标志，未启动真实传输
  s_dcmi_dma.XferCpltCallback=Test_DmaComplete;
  DMA1_Stream0->CR|=DMA_IT_TC;DMA1->LISR=DMA_FLAG_TCIF0_4;
  DMA1_Stream0_IRQHandler();EQ(g_result.dma_callback,1);
  *((volatile uint32_t *)&DCMI->MISR)=DCMI_FLAG_LINERI;
  DCMI_IRQHandler();EQ(g_result.dcmi_callback,1);
  EQ(DCMI->CR&(DCMI_CR_CAPTURE|DCMI_CR_ENABLE),0);EQ(DMA1_Stream0->CR&DMA_SxCR_EN,0);
 }
 return 0;
}
int Dcmi_TestRun(uint32_t id)
{
 int result=Cases(id);
 g_result.dma_cr=DMA1_Stream0->CR;g_result.dcmi_cr=DCMI->CR;
 g_result.request=s_dcmi_dma.Init.Request;g_result.ready=s_dcmi_ready;
 return result;
}

uint32_t Platform_TimeGetMs(void){return HAL_GetTick();}
