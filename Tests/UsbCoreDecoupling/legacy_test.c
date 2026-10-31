#define s_started bsp_started
#define s_callbacks bsp_callbacks
#include "../../CM7/BSP/bsp_usb.c"
#undef s_started
#undef s_callbacks
#include "../../CM7/Platform/usb_device.c"
#include "../../CM7/Service/usb_test_device.c"
#include <string.h>
typedef struct {uint32_t fail_line,checks,operations,replaces,deferred,case_id;} TestResult;
volatile TestResult g_result;
static uint32_t pending_in;
static SysResult_t reentry_from_start;
static uint32_t seen_rx_fifo,seen_tx_fifo[2],last_ep,close_count,flush_count;
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
void HAL_NVIC_EnableIRQ(IRQn_Type irq){(void)irq;nvic=1;NVIC->ISER[((uint32_t)irq)>>5] |= 1UL << (((uint32_t)irq)&31U);if(pending_in){pending_in=0;HAL_PCD_DataInStageCallback(&s_pcd,1);}}
void HAL_NVIC_DisableIRQ(IRQn_Type irq){(void)irq;nvic=0;NVIC->ISER[((uint32_t)irq)>>5] &= ~(1UL << (((uint32_t)irq)&31U));}
void HAL_NVIC_ClearPendingIRQ(IRQn_Type irq){(void)irq;}
HAL_StatusTypeDef HAL_PCD_Init(PCD_HandleTypeDef *p){init_count++;HAL_PCD_MspInit(p);return fault==4?HAL_ERROR:HAL_OK;}
HAL_StatusTypeDef HAL_PCD_DeInit(PCD_HandleTypeDef *p){if(fault==11)return HAL_ERROR;HAL_PCD_MspDeInit(p);return HAL_OK;}
HAL_StatusTypeDef HAL_PCDEx_SetRxFiFo(PCD_HandleTypeDef *p,uint16_t n){(void)p;seen_rx_fifo=n;return fault==5 || n!=128?HAL_ERROR:HAL_OK;}
HAL_StatusTypeDef HAL_PCDEx_SetTxFiFo(PCD_HandleTypeDef *p,uint8_t ep,uint16_t n){(void)p;if(ep<2)seen_tx_fifo[ep]=n;return fault==6 || (fault==12 && ep==1) || ep>1 || n!=(ep?128:64)?HAL_ERROR:HAL_OK;}
HAL_StatusTypeDef HAL_PCD_Start(PCD_HandleTypeDef *p){(void)p;start_count++;if(fault==18)reentry_from_start=USB_Device_Stop();return fault==7?HAL_BUSY:HAL_OK;}
HAL_StatusTypeDef HAL_PCD_Stop(PCD_HandleTypeDef *p){(void)p;stop_count++;return fault==8?HAL_ERROR:HAL_OK;}
HAL_StatusTypeDef HAL_PCD_EP_Open(PCD_HandleTypeDef *p,uint8_t ep,uint16_t size,uint8_t type){PCD_EPTypeDef *e=(ep&0x80)?&p->IN_ep[ep&15]:&p->OUT_ep[ep&15];last_ep=ep;open_count++;if(fault==13)return HAL_ERROR;e->maxpacket=size;e->type=type;return HAL_OK;}
HAL_StatusTypeDef HAL_PCD_EP_Close(PCD_HandleTypeDef *p,uint8_t ep){(void)p;last_ep=ep;close_count++;if(fault==17 && ep==0x81)pending_in=1;return fault==14?HAL_ERROR:HAL_OK;}
HAL_StatusTypeDef HAL_PCD_EP_Flush(PCD_HandleTypeDef *p,uint8_t ep){(void)p;last_ep=ep;flush_count++;return fault==15?HAL_ERROR:HAL_OK;}
HAL_StatusTypeDef HAL_PCD_EP_SetStall(PCD_HandleTypeDef *p,uint8_t ep){stall_count++;if(ep&0x80)p->IN_ep[ep&15].is_stall=1;else p->OUT_ep[ep&15].is_stall=1;return HAL_OK;}
HAL_StatusTypeDef HAL_PCD_EP_ClrStall(PCD_HandleTypeDef *p,uint8_t ep){if(ep&0x80)p->IN_ep[ep&15].is_stall=0;else p->OUT_ep[ep&15].is_stall=0;return HAL_OK;}
HAL_StatusTypeDef HAL_PCD_SetAddress(PCD_HandleTypeDef *p,uint8_t a){(void)p;address_value=a;return HAL_OK;}
HAL_StatusTypeDef HAL_PCD_EP_Transmit(PCD_HandleTypeDef *p,uint8_t ep,uint8_t *data,uint32_t n){uint32_t i;last_ep=ep;if(fault==16 && ep==0x81){if(NVIC_GetEnableIRQ(OTG_FS_IRQn))HAL_PCD_DataInStageCallback(p,1);else pending_in=1;}tx_size=n;p->IN_ep[ep&15].xfer_buff=data;for(i=0;i<n && i<128;i++)tx_data[i]=data[i];return fault==9?HAL_BUSY:HAL_OK;}
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
int Stream_TestRun(uint32_t id)
{
 uint32_t i;void *memory;PCD_HandleTypeDef foreign={0};
 g_result.case_id=id;
 RCC->PLL1DIVR=0x12345678;RCC->PLL2DIVR=0x23456789;RCC->PLL3DIVR=0x34567890;
 if(id==0){EQ(USB_Device_Start(),SYS_ERR_INVALID_STATE);EQ(USB_Device_Stop(),SYS_ERR_INVALID_STATE);EQ(USB_Device_IsConfigured(),false);return 0;}
 if(id>=2 && id<=7){fault=id-1;EQ(BSP_USB_Init(),id==3?SYS_ERR_TIMEOUT:id==4?SYS_ERR_INVALID_STATE:SYS_ERR_IO);EQ(s_ready,false);EQ(nvic,0);return 0;}
 EQ(BSP_USB_Init(),SYS_OK);EQ(USB_Device_Init(),SYS_OK);EQ(Usb_TestRegister(),SYS_OK);EQ(init_count,1);EQ(nvic,0);
 if(id==1){
  EQ(s_pcd.Instance,USB2_OTG_FS);EQ(s_pcd.Init.phy_itface,PCD_PHY_EMBEDDED);EQ(s_pcd.Init.speed,PCD_SPEED_FULL);EQ(s_pcd.Init.dma_enable,0);EQ(s_pcd.Init.vbus_sensing_enable,0);
  EQ(seen_gpio.Pin,GPIO_PIN_11|GPIO_PIN_12);EQ(seen_gpio.Alternate,GPIO_AF10_OTG2_FS);EQ(priority,6);
  EQ(seen_osc.OscillatorType,RCC_OSCILLATORTYPE_HSI48);EQ(seen_osc.PLL.PLLState,RCC_PLL_NONE);EQ(seen_clock.UsbClockSelection,RCC_USBCLKSOURCE_HSI48);EQ(seen_crs.Source,RCC_CRS_SYNC_SOURCE_USB2);EQ(seen_crs.ReloadValue,47999);
  EQ(RCC->PLL1DIVR,0x12345678);EQ(RCC->PLL2DIVR,0x23456789);EQ(RCC->PLL3DIVR,0x34567890);return 0;
 }
 if(id==8){EQ(USB_Device_Init(),SYS_OK);EQ(Usb_TestRegister(),SYS_OK);EQ(init_count,1);EQ(s_device.pClass[0],&s_class_driver);return 0;}
 if(id==10){fault=7;EQ(USB_Device_Start(),SYS_ERR_BUSY);EQ(nvic,0);EQ(s_started,0);return 0;}
 if(id==37){EQ(USB_Device_RegisterClass(NULL,&s_descriptors),SYS_ERR_INVALID_ARG);EQ(USB_Device_RegisterClass(&s_base_class,NULL),SYS_ERR_INVALID_ARG);return 0;}
 if(id==39){EQ(USB_Device_RegisterClass(&s_base_class,&s_descriptors),SYS_OK);return 0;}
 if(id==40){EQ(USB_Device_Alloc(1025),NULL);memory=USB_Device_Alloc(1024);EQ(memory!=NULL,1);EQ(USB_Device_Alloc(1),NULL);USB_Device_Free(memory);EQ(USB_Device_Alloc(1),memory);return 0;}
 EQ(USB_Device_Start(),SYS_OK);EQ(nvic,1);HAL_PCD_ResetCallback(&s_pcd);
 if(id==9){EQ(USB_Device_Start(),SYS_ERR_BUSY);EQ(USB_Device_Stop(),SYS_OK);EQ(nvic,0);return 0;}
 if(id==11){fault=8;EQ(USB_Device_Stop(),SYS_ERR_IO);EQ(USB_Device_GetLastResult(),SYS_ERR_IO);EQ(s_started,true);return 0;}
 if(id==12){EQ(open_count,2);EQ(s_device.dev_speed,USBD_SPEED_FULL);EQ(s_device.dev_state,USBD_STATE_DEFAULT);return 0;}
 if(id==13 || id==14){Request(0x80,USB_REQ_GET_DESCRIPTOR,0x100,0,id==13?18:8);EQ(tx_size,id==13?18:8);EQ(tx_data[0],18);EQ(tx_data[1],1);EQ(tx_data[7],64);EQ(stall_count,0);return 0;}
 if(id==15 || id==16){Request(0x80,USB_REQ_GET_DESCRIPTOR,0x200,0,id==15?9:18);EQ(tx_size,id==15?9:18);EQ(tx_data[2],18);EQ(tx_data[4],1);if(id==16){EQ(tx_data[13],0);EQ(tx_data[14],0xFF);}return 0;}
 if(id>=17 && id<=20){Request(0x80,USB_REQ_GET_DESCRIPTOR,0x300+id-17,0x409,64);EQ(tx_data[1],USB_DESC_TYPE_STRING);EQ(tx_size>0,1);EQ(stall_count,0);return 0;}
 if(id==21){Request(0,USB_REQ_SET_ADDRESS,7,0,0);EQ(address_value,7);EQ(s_device.dev_state,USBD_STATE_ADDRESSED);EQ(tx_size,0);return 0;}
 if(id==22){Request(0,USB_REQ_SET_ADDRESS,128,0,0);EQ(stall_count>0,1);EQ(s_device.dev_state,USBD_STATE_DEFAULT);return 0;}
 if(id==23){Request(0,USB_REQ_SET_CONFIGURATION,1,0,0);EQ(stall_count>0,1);EQ(USB_Device_IsConfigured(),false);return 0;}
 Configured();EQ(USB_Device_IsConfigured(),true);
 if(id==24){EQ(s_device.dev_config,1);return 0;}
 if(id==25){Request(0,USB_REQ_SET_CONFIGURATION,0,0,0);EQ(s_device.dev_state,USBD_STATE_ADDRESSED);EQ(USB_Device_IsConfigured(),false);return 0;}
 if(id==26){Request(0x80,USB_REQ_GET_CONFIGURATION,0,0,1);EQ(tx_size,1);EQ(tx_data[0],1);return 0;}
 if(id==27){Request(0x80,USB_REQ_GET_STATUS,0,0,2);EQ(tx_size,2);EQ(tx_data[0],0);return 0;}
 if(id==28){Request(0x80,USB_REQ_GET_DESCRIPTOR,0x9900,0,8);EQ(stall_count>0,1);return 0;}
 if(id==29 || id==30){Request(id==29?0xC1:0xA1,id==29?0x55:0x81,0x100,0,26);EQ(stall_count>0,1);return 0;}
 if(id==31){Request(1,USB_REQ_SET_INTERFACE,1,0,0);EQ(stall_count>0,1);return 0;}
 if(id==32){Request(0x81,USB_REQ_GET_INTERFACE,0,0,1);EQ(tx_size,1);EQ(tx_data[0],0);return 0;}
 if(id==33){HAL_PCD_SuspendCallback(&s_pcd);EQ(s_device.dev_state,USBD_STATE_SUSPENDED);HAL_PCD_ResumeCallback(&s_pcd);EQ(s_device.dev_state,USBD_STATE_CONFIGURED);return 0;}
 if(id==34){HAL_PCD_ResetCallback(&s_pcd);EQ(s_device.dev_config,0);EQ(USB_Device_IsConfigured(),false);return 0;}
 if(id==35){EQ(USB_Device_Stop(),SYS_OK);EQ(USB_Device_IsConfigured(),false);EQ(USB_Device_Start(),SYS_OK);EQ(USB_Device_IsConfigured(),false);HAL_PCD_ResetCallback(&s_pcd);Configured();EQ(USB_Device_IsConfigured(),true);return 0;}
 if(id==36){EQ(USBD_DeInit(&s_device),USBD_OK);EQ(s_ready,false);EQ(nvic,0);EQ(bsp_callbacks,NULL);return 0;}
 if(id==38){EQ(USB_Device_RegisterClass(&s_base_class,&s_descriptors),SYS_ERR_INVALID_STATE);return 0;}
 if(id==41){EQ(BSP_USB_OpenEp(0x81,EP_TYPE_ISOC,64),SYS_ERR_NOT_SUPPORTED);return 0;}
 if(id==42){fault=9;EQ(USBD_LL_Transmit(&s_device,0x80,tx_data,8),USBD_BUSY);EQ(USB_Device_GetLastResult(),SYS_ERR_BUSY);EQ(USB_Device_IsConfigured(),true);return 0;}
 if(id==43){i=s_device.dev_state;HAL_PCD_ResetCallback(&foreign);HAL_PCD_SuspendCallback(&foreign);EQ(s_device.dev_state,i);return 0;}
 if(id==44){OTG_FS_IRQHandler();EQ(irq_count,1);return 0;}
 if(id==45){Request(0x80,USB_REQ_GET_DESCRIPTOR,0x600,0,10);EQ(stall_count>0,1);return 0;}
 if(id==46){EQ(BSP_USB_StallEp(0x80),SYS_OK);EQ(BSP_USB_IsStallEp(0x80),true);EQ(BSP_USB_ClearStallEp(0x80),SYS_OK);EQ(BSP_USB_IsStallEp(0x80),false);return 0;}
 if(id==47){Request(0x80,USB_REQ_GET_DESCRIPTOR,0x100,0,18);EQ(s_device.ep0_state,USBD_EP0_DATA_IN);HAL_PCD_DataInStageCallback(&s_pcd,0);EQ(s_device.ep0_state,USBD_EP0_STATUS_OUT);EQ(rx_size,0);HAL_PCD_DataOutStageCallback(&s_pcd,0);return 0;}
 if(id==48){Request(0x80,USB_REQ_GET_DESCRIPTOR,0x200,0,255);EQ(tx_size,18);return 0;}
 if(id==49){Request(0,USB_REQ_SET_CONFIGURATION,2,0,0);EQ(stall_count>0,1);return 0;}
 if(id==50){USBD_ClassTypeDef driver=s_base_class;USBD_DescriptorsTypeDef desc=s_descriptors;EQ(USB_Device_Stop(),SYS_OK);desc.GetSerialStrDescriptor=NULL;EQ(USB_Device_RegisterClass(&driver,&desc),SYS_ERR_INVALID_ARG);return 0;}
 if(id==51){EQ(USB_Device_Stop(),SYS_OK);EQ(USB_Device_RegisterClass(&s_base_class,&s_descriptors),SYS_OK);EQ(USB_Device_RegisterClass(&s_base_class,&s_descriptors),SYS_OK);EQ(s_device.NumClasses,1);return 0;}
 EQ(0,1);return 1;
}
