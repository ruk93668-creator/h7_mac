#define s_started bsp_started
#define s_callbacks bsp_callbacks
#include "../../CM7/BSP/bsp_usb.c"
#undef s_started
#undef s_callbacks
#include "../../CM7/Platform/usb_device.c"
#include <string.h>
typedef struct {uint32_t fail_line,checks,operations,replaces,deferred,case_id;} TestResult;
volatile TestResult g_result;
static uint32_t fault,nvic,priority,init_count,start_count,stop_count,irq_count,open_count,stall_count,address_value,tx_size,rx_size;
static uint8_t tx_data[128];
static RCC_OscInitTypeDef seen_osc;
static RCC_PeriphCLKInitTypeDef seen_clock;
static RCC_CRSInitTypeDef seen_crs;
static GPIO_InitTypeDef seen_gpio;
void *memcpy(void *d,const void *s,size_t n){uint8_t *p=d;const uint8_t *q=s;while(n--)*p++=*q++;return d;}
void *memset(void *d,int v,size_t n){uint8_t *p=d;while(n--)*p++=(uint8_t)v;return d;}
int memcmp(const void *a,const void *b,size_t n){const uint8_t *p=a,*q=b;while(n--){if(*p!=*q)return *p-*q;p++;q++;}return 0;}
void __aeabi_memclr4(void *d,size_t n){volatile uint8_t *p=d;while(n--)*p++=0;}
void __aeabi_memclr8(void *d,size_t n){volatile uint8_t *p=d;while(n--)*p++=0;}
void __aeabi_memclr(void *d,size_t n){volatile uint8_t *p=d;while(n--)*p++=0;}
void __aeabi_memcpy(void *d,const void *s,size_t n){volatile uint8_t *p=d;const volatile uint8_t *q=s;while(n--)*p++=*q++;}
void __aeabi_memcpy4(void *d,const void *s,size_t n){volatile uint8_t *p=d;const volatile uint8_t *q=s;while(n--)*p++=*q++;}

static int Eq(uint32_t line,uint32_t a,uint32_t b){g_result.checks++;if(a!=b){if(!g_result.fail_line)g_result.fail_line=line;return 0;}return 1;}
#define EQ(a,b) do{if(!Eq(__LINE__,(uint32_t)(a),(uint32_t)(b)))return 1;}while(0)
HAL_StatusTypeDef HAL_RCC_OscConfig(RCC_OscInitTypeDef *c){seen_osc=*c;return fault==1?HAL_ERROR:HAL_OK;}
HAL_StatusTypeDef HAL_RCCEx_PeriphCLKConfig(RCC_PeriphCLKInitTypeDef *c){seen_clock=*c;return fault==2?HAL_TIMEOUT:HAL_OK;}
uint32_t HAL_RCCEx_GetPeriphCLKFreq(uint64_t p){(void)p;return fault==3?47000000:48000000;}
void HAL_RCCEx_CRSConfig(const RCC_CRSInitTypeDef *c){seen_crs=*c;}
HAL_StatusTypeDef HAL_PWREx_DisableUSBReg(void){return HAL_OK;}
void HAL_PWREx_EnableUSBVoltageDetector(void){}
void HAL_GPIO_Init(GPIO_TypeDef *p,const GPIO_InitTypeDef *g){(void)p;seen_gpio=*g;}
void HAL_GPIO_DeInit(GPIO_TypeDef *p,uint32_t pins){(void)p;(void)pins;}
void HAL_NVIC_SetPriority(IRQn_Type irq,uint32_t pre,uint32_t sub){(void)irq;(void)sub;priority=pre;}
void HAL_NVIC_EnableIRQ(IRQn_Type irq){(void)irq;nvic=1;}
void HAL_NVIC_DisableIRQ(IRQn_Type irq){(void)irq;nvic=0;}
void HAL_NVIC_ClearPendingIRQ(IRQn_Type irq){(void)irq;}
HAL_StatusTypeDef HAL_PCD_Init(PCD_HandleTypeDef *p){init_count++;HAL_PCD_MspInit(p);return fault==4?HAL_ERROR:HAL_OK;}
HAL_StatusTypeDef HAL_PCD_DeInit(PCD_HandleTypeDef *p){if(fault==11)return HAL_ERROR;HAL_PCD_MspDeInit(p);return HAL_OK;}
HAL_StatusTypeDef HAL_PCDEx_SetRxFiFo(PCD_HandleTypeDef *p,uint16_t n){(void)p;return fault==5 || n!=320?HAL_ERROR:HAL_OK;}
HAL_StatusTypeDef HAL_PCDEx_SetTxFiFo(PCD_HandleTypeDef *p,uint8_t ep,uint16_t n){(void)p;return fault==6 || ep>=9 || n<16?HAL_ERROR:HAL_OK;}
HAL_StatusTypeDef HAL_PCD_Start(PCD_HandleTypeDef *p){(void)p;start_count++;return fault==7?HAL_BUSY:HAL_OK;}
HAL_StatusTypeDef HAL_PCD_Stop(PCD_HandleTypeDef *p){(void)p;stop_count++;return fault==8?HAL_ERROR:HAL_OK;}
HAL_StatusTypeDef HAL_PCD_EP_Open(PCD_HandleTypeDef *p,uint8_t ep,uint16_t size,uint8_t type){USB_OTG_EPTypeDef *e=(ep&0x80U)?&p->IN_ep[ep&15U]:&p->OUT_ep[ep&15U];e->maxpacket=size;e->type=type;open_count++;return HAL_OK;}
HAL_StatusTypeDef HAL_PCD_EP_Close(PCD_HandleTypeDef *p,uint8_t ep){(void)p;(void)ep;return HAL_OK;}
HAL_StatusTypeDef HAL_PCD_EP_Flush(PCD_HandleTypeDef *p,uint8_t ep){(void)p;(void)ep;return HAL_OK;}
HAL_StatusTypeDef HAL_PCD_EP_SetStall(PCD_HandleTypeDef *p,uint8_t ep){stall_count++;if(ep&0x80)p->IN_ep[0].is_stall=1;else p->OUT_ep[0].is_stall=1;return HAL_OK;}
HAL_StatusTypeDef HAL_PCD_EP_ClrStall(PCD_HandleTypeDef *p,uint8_t ep){if(ep&0x80)p->IN_ep[0].is_stall=0;else p->OUT_ep[0].is_stall=0;return HAL_OK;}
HAL_StatusTypeDef HAL_PCD_SetAddress(PCD_HandleTypeDef *p,uint8_t a){(void)p;address_value=a;return HAL_OK;}
HAL_StatusTypeDef HAL_PCD_EP_Transmit(PCD_HandleTypeDef *p,uint8_t ep,uint8_t *data,uint32_t n){uint32_t i;(void)ep;tx_size=n;p->IN_ep[0].xfer_buff=data;for(i=0;i<n && i<128;i++)tx_data[i]=data[i];return fault==9?HAL_BUSY:HAL_OK;}
HAL_StatusTypeDef HAL_PCD_EP_Receive(PCD_HandleTypeDef *p,uint8_t ep,uint8_t *data,uint32_t n){(void)ep;p->OUT_ep[0].xfer_buff=data;rx_size=n;return HAL_OK;}
uint32_t HAL_PCD_EP_GetRxCount(PCD_HandleTypeDef const *p,uint8_t ep){(void)p;(void)ep;return rx_size;}
void HAL_PCD_IRQHandler(PCD_HandleTypeDef *p){(void)p;irq_count++;}
void HAL_Delay(uint32_t n){(void)n;}
static void Request(uint8_t bm,uint8_t req,uint16_t val,uint16_t idx,uint16_t len)
{
 uint8_t *p=(uint8_t *)s_pcd.Setup;
 p[0]=bm;p[1]=req;p[2]=val;p[3]=val>>8;p[4]=idx;p[5]=idx>>8;p[6]=len;p[7]=len>>8;
 s_pcd.IN_ep[0].is_stall=0;s_pcd.OUT_ep[0].is_stall=0;tx_size=999;stall_count=0;
 HAL_PCD_SetupStageCallback(&s_pcd);
}
static void Configured(void){Request(0,USB_REQ_SET_ADDRESS,7,0,0);Request(0,USB_REQ_SET_CONFIGURATION,1,0,0);}

//P1-B stop APIs are not driven by legacy scenarios; specialist tests provide a controllable clock.
uint32_t HAL_GetTick(void) { return 0U; }
