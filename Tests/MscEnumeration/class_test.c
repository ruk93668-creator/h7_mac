#include "usb_device.h"
#include "usb_msc_class.h"
#include "usb_msc_desc.h"
#include "usbd_ioreq.h"
#include <string.h>
static USBD_ClassTypeDef *driver;
static USBD_DescriptorsTypeDef *descriptors;
static unsigned opens, closes, fail_open, fail_close, send_length, failed_line, checks;
static unsigned addresses[2];
SysResult_t USB_Device_RegisterClass(USBD_ClassTypeDef *c, USBD_DescriptorsTypeDef *d)
{ driver=c; descriptors=d; return SYS_OK; }
SysResult_t USB_Device_OpenEp(uint8_t ep,uint8_t type,uint16_t size)
{ if(type!=USBD_EP_TYPE_BULK || size!=64) return SYS_ERR_INVALID_ARG;
  addresses[opens%2]=ep; ++opens; return opens==fail_open ? SYS_ERR_IO : SYS_OK; }
SysResult_t USB_Device_CloseEp(uint8_t ep)
{ ++closes; return ep==fail_close ? SYS_ERR_IO : SYS_OK; }
USBD_StatusTypeDef USBD_CtlSendData(USBD_HandleTypeDef *d,uint8_t *p,uint32_t n)
{ (void)d; send_length=n; if(p[0]!=0 || (n==2 && p[1]!=0)) return USBD_FAIL; return USBD_OK; }
void USBD_GetString(uint8_t *s,uint8_t *p,uint16_t *n)
{ unsigned i=0; while(s[i]) {p[2+i*2]=s[i];p[3+i*2]=0;++i;} p[0]=(uint8_t)(2+i*2);p[1]=3;*n=p[0]; }
#define CHECK(x) do {++checks; if(!(x)) {failed_line=__LINE__; return failed_line;}} while(0)
unsigned Test_Run(void)
{
 USBD_HandleTypeDef d; USBD_SetupReqTypedef q; uint16_t n; uint8_t *b;
 memset(&d,0,sizeof(d)); memset(&q,0,sizeof(q));
 CHECK(USB_MSC_Register()==SYS_OK); CHECK(driver && descriptors);
 b=driver->GetFSConfigDescriptor(&n); CHECK(n==32); CHECK(b[2]==n && b[3]==0 && b[4]==1);
 CHECK(b[9]==9 && b[10]==4 && b[11]==0 && b[12]==0 && b[13]==2);
 CHECK(b[14]==8 && b[15]==6 && b[16]==0x50);
 CHECK(b[18]==7 && b[19]==5 && b[20]==0x82 && b[21]==2 && b[22]==64 && b[23]==0);
 CHECK(b[25]==7 && b[26]==5 && b[27]==2 && b[28]==2 && b[29]==64 && b[30]==0);
 CHECK(b[20]!=b[27]); CHECK(USB_MSC_GetFSConfigDescriptor(0)==0);
 b=descriptors->GetDeviceDescriptor(USBD_SPEED_FULL,&n);CHECK(b && n==18 && b[7]==64 && b[17]==1);
 CHECK(descriptors->GetDeviceDescriptor(USBD_SPEED_HIGH,&n)==0 && n==0);
 CHECK(driver->GetDeviceQualifierDescriptor(&n)==0 && n==0);
 CHECK(driver->Init(&d,2)==USBD_FAIL && opens==0);
 fail_open=2; CHECK(driver->Init(&d,1)==USBD_FAIL && closes==1);
 opens=closes=0; fail_open=0; CHECK(driver->Init(&d,1)==USBD_OK && opens==2);
 CHECK(addresses[0]==0x82 && addresses[1]==2); CHECK(driver->Init(&d,1)==USBD_FAIL && opens==2);
 d.dev_state=USBD_STATE_CONFIGURED;
 q.bmRequest=0x81;q.bRequest=USB_REQ_GET_INTERFACE;q.wLength=1;
 CHECK(driver->Setup(&d,&q)==USBD_OK && send_length==1);
 q.bRequest=USB_REQ_GET_STATUS;q.wLength=2;CHECK(driver->Setup(&d,&q)==USBD_OK && send_length==2);
 q.bmRequest=1;q.bRequest=USB_REQ_SET_INTERFACE;q.wLength=0;
 CHECK(driver->Setup(&d,&q)==USBD_OK);q.wValue=1;CHECK(driver->Setup(&d,&q)==USBD_FAIL);q.wValue=0;
 q.bmRequest=0xa1;q.bRequest=0xfe;q.wLength=1;CHECK(driver->Setup(&d,&q)==USBD_FAIL);
 q.bmRequest=0x21;q.bRequest=0xff;q.wLength=0;CHECK(driver->Setup(&d,&q)==USBD_FAIL);
 q.bmRequest=0x81;q.bRequest=USB_REQ_GET_INTERFACE;q.wLength=1;q.wIndex=0x100;
 CHECK(driver->Setup(&d,&q)==USBD_FAIL);q.wIndex=0;q.wValue=1;CHECK(driver->Setup(&d,&q)==USBD_FAIL);
 q.bmRequest=2;q.bRequest=USB_REQ_CLEAR_FEATURE;q.wValue=0;q.wLength=0;q.wIndex=0x82;
 CHECK(driver->Setup(&d,&q)==USBD_OK);q.wIndex=0x81;CHECK(driver->Setup(&d,&q)==USBD_FAIL);
 fail_close=2;CHECK(driver->DeInit(&d,1)==USBD_FAIL);CHECK(driver->Setup(&d,&q)==USBD_FAIL);
 fail_close=0;CHECK(driver->DeInit(&d,1)==USBD_OK);CHECK(driver->DeInit(&d,1)==USBD_OK);
 CHECK(driver->Init(&d,1)==USBD_OK);CHECK(driver->DeInit(&d,1)==USBD_OK);
 return 0;
}
unsigned Test_Checks(void) {return checks;}
