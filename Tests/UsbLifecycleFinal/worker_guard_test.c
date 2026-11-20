//Real Storage mailbox/BOT/Class; USB Close deliberately refuses (not a fake USB proof).
#define USB_MODE_BOOT_MSC 1
#define USB_MSC_SD_TEST 1
#define USB_MSC_SD_WRITE_ENABLE 0
#include "../MscBlockAdapter/adapter_test.c"
#include "../../CM7/Service/usb_msc_bot.c"
#include "../../CM7/Service/usb_msc_class.c"
static uint32_t unexpected_calls;
uint32_t Platform_TimeGetMs(void){++unexpected_calls;return 0U;}
USBD_StatusTypeDef USBD_CtlSendData(USBD_HandleTypeDef *d,uint8_t *b,uint32_t n){(void)d;(void)b;(void)n;++unexpected_calls;return USBD_FAIL;}
SysResult_t USB_Device_AbortEp(uint8_t e){(void)e;++unexpected_calls;return SYS_ERR_NOT_SUPPORTED;}
SysResult_t USB_Device_CompleteControlStatus(uint32_t t){(void)t;++unexpected_calls;return SYS_ERR_NOT_SUPPORTED;}
SysResult_t USB_Device_DeferControlStatus(const USBD_SetupReqTypedef *r,uint32_t *t){(void)r;(void)t;++unexpected_calls;return SYS_ERR_NOT_SUPPORTED;}
SysResult_t USB_Device_FailControlStatus(uint32_t t){(void)t;++unexpected_calls;return SYS_ERR_NOT_SUPPORTED;}
SysResult_t USB_Device_GetControlStatus(uint32_t t,UsbControlStatus_t *s){(void)t;(void)s;++unexpected_calls;return SYS_ERR_NOT_SUPPORTED;}
SysResult_t USB_Device_GetEpState(uint8_t e,UsbDeviceEndpointState_t *s){(void)e;(void)s;++unexpected_calls;return SYS_ERR_NOT_SUPPORTED;}
uint32_t USB_Device_GetInCompleteSequence(uint8_t e){(void)e;++unexpected_calls;return 0U;}
uint32_t USB_Device_GetInSequence(uint8_t e){(void)e;++unexpected_calls;return 0U;}
uint32_t USB_Device_GetRxSize(uint8_t e){(void)e;++unexpected_calls;return 0U;}
uint32_t USB_Device_Lock(void){++unexpected_calls;return 0U;}
void USB_Device_Unlock(uint32_t m){(void)m;++unexpected_calls;}
SysResult_t USB_Device_Receive(uint8_t e,uint8_t *p,uint32_t n){(void)e;(void)p;(void)n;++unexpected_calls;return SYS_ERR_NOT_SUPPORTED;}
SysResult_t USB_Device_Transmit(uint8_t e,uint8_t *p,uint32_t n){(void)e;(void)p;(void)n;++unexpected_calls;return SYS_ERR_NOT_SUPPORTED;}
SysResult_t USB_Device_RegisterClass(USBD_ClassTypeDef *c,USBD_DescriptorsTypeDef *d){(void)c;(void)d;++unexpected_calls;return SYS_ERR_NOT_SUPPORTED;}
SysResult_t USB_Device_StallEp(uint8_t e){(void)e;++unexpected_calls;return SYS_ERR_NOT_SUPPORTED;}
bool USB_Device_WasControlStatusComplete(uint32_t t){(void)t;++unexpected_calls;return false;}
USBD_DescriptorsTypeDef *USB_MSC_GetDescriptors(void){++unexpected_calls;return NULL;}
uint8_t *USB_MSC_GetFSConfigDescriptor(uint16_t *n){(void)n;++unexpected_calls;return NULL;}
void USB_MSC_ScsiSense(MscScsiContext_t *c,uint8_t k,uint8_t a,uint8_t q){(void)c;(void)k;(void)a;(void)q;++unexpected_calls;}
bool USB_MSC_ScsiDecode(MscScsiContext_t *c,const MscScsiMedia_t *m,const uint8_t *b,uint8_t n,MscScsiCommand_t *o){(void)c;(void)m;(void)b;(void)n;(void)o;++unexpected_calls;return false;}
static uint32_t close_calls;
SysResult_t USB_Device_OpenEp(uint8_t ep,uint8_t type,uint16_t size){(void)ep;(void)type;(void)size;return SYS_OK;}
SysResult_t USB_Device_CloseEp(uint8_t ep){(void)ep;++close_calls;return SYS_ERR_BUSY;}
volatile uint32_t g_worker_guard[9];
int Worker_Step(uint32_t op,uint32_t arg)
{
 uint32_t result=0U;(void)arg;
 if(op==0U)
 {
  result=USB_MSC_StorageInit(&s_device);s_bot.backend=USB_MSC_StorageBackend();s_bot.epoch=1U;s_bot.active=true;s_in_open=true;s_out_open=true;s_configured=true;
  s_test_request.epoch=1U;s_test_request.token=1U;s_test_request.generation=1U;s_test_request.operation=MSC_SCSI_READ;s_test_request.blocks=2U;s_test_request.buffer=s_data;
  result=s_bot.backend->submit(&s_test_request);
 }
 else if(op==1U){s_quiet=false;USB_MSC_StorageProcess();}
 else if(op==2U)result=Msc_DeInit(NULL,1U);
 else if(op==3U){s_quiet=true;USB_MSC_StorageProcess();}
 else if(op==4U){s_exception=16U;result=Msc_DeInit(NULL,1U);s_exception=0U;}
 g_worker_guard[0]=result;g_worker_guard[1]=s_storage.state;g_worker_guard[2]=USB_MSC_BotBackendDrained();g_worker_guard[3]=close_calls;
 g_worker_guard[4]=s_storage.request.epoch;g_worker_guard[5]=s_in_open;g_worker_guard[6]=s_out_open;g_worker_guard[7]=s_mask;g_worker_guard[8]=unexpected_calls;
 return 0;
}
