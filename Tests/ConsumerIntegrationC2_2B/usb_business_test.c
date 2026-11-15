#include "../UsbModuleB2/behavior_test.c"
#include "video_config.h"
static VideoConfig_t product={{320,240,CAMERA_PIXEL_FORMAT_JPEG},VIDEO_OUTPUT_USB_UVC,25,70000};
static AppLifecycleMessage_t captured;
static uint32_t sent_requests;
static SysResult_t send_error;
const VideoConfig_t *VideoConfig_GetDefault(void) { return &product; }
//只模拟运输，不模拟USB业务或修改App_USB私有状态
bool App_LifecycleMessagesReceive(AppLifecycleChannel_t c,AppLifecycleMessage_t*m)
{ (void)c;(void)m;return false; }
SysResult_t App_LifecycleMessagesSend(AppLifecycleChannel_t c,const AppLifecycleMessage_t*m)
{ (void)c;if(send_error!=SYS_OK)return send_error;captured=*m;sent_requests++;return SYS_OK; }
static void Reply(SysResult_t result)
{
 AppLifecycleMessage_t reply;
 if(App_LifecycleMakeReply(&captured,1U,result,&reply)!=SYS_OK){g_failure=__LINE__;return;}
 if(App_USBReceiveLifecycle(&reply)!=SYS_OK){g_failure=__LINE__;return;}
 Round(0);
}
#define VERIFY(x) do{if(!(x)){g_failure=__LINE__;return g_failure;}}while(0)
static void BeginUsb(void){Push(APP_MESSAGE_START,1);Round(RUNTIME_EVENT_BOOT_READY);}
static void GrantUsb(void){BeginUsb();Reply(SYS_OK);Reply(SYS_OK);}
int UsbBusiness_Run(uint32_t id)
{
 AppLifecycleMessage_t stale;uint32_t first;
 App_USBSetDiagnosticObserver(UsbTask_Observe);
 if(id==4){product.output=VIDEO_OUTPUT_USB_TEST;mode=USB_MODE_TEST;BeginUsb();VERIFY(s_usb.started&&sent_requests==0);return 0;}
 if(id==0){BeginUsb();VERIFY(!s_usb.started&&captured.operation==APP_LIFECYCLE_SESSION_QUERY);Reply(SYS_OK);VERIFY(!s_usb.started&&captured.operation==APP_LIFECYCLE_ACQUIRE);Reply(SYS_OK);VERIFY(s_usb.started&&s_usb.camera.granted);return 0;}
 if(id==1){BeginUsb();Reply(SYS_OK);Reply(SYS_ERR_IO);VERIFY(!s_usb.started&&s_usb.last_result==SYS_ERR_IO&&s_usb.camera.granted==0);return 0;}
 if(id==2){start_result=SYS_ERR_IO;GrantUsb();VERIFY(!s_usb.started&&s_usb.camera.phase==APP_CONSUMER_RELEASE&&s_usb.camera.granted==1);return 0;}
 if(id==5){send_error=SYS_ERR_NO_RESOURCE;BeginUsb();first=s_usb.camera.request.identity.request_id;Round(0);VERIFY(!s_usb.started&&s_usb.camera.request.identity.request_id==first);send_error=SYS_OK;Round(0);VERIFY(captured.identity.request_id==first);return 0;}
 if(id==6){BeginUsb();stale=captured;stale.identity.consumer_generation++;VERIFY(App_LifecycleMakeReply(&stale,1,SYS_OK,&stale)==SYS_OK);VERIFY(App_USBReceiveLifecycle(&stale)!=SYS_OK);VERIFY(!s_usb.started);return 0;}
 if(id==9){BeginUsb();owner=false;VERIFY(App_LifecycleMakeReply(&captured,1,SYS_OK,&stale)==SYS_OK);VERIFY(App_USBReceiveLifecycle(&stale)==SYS_ERR_INVALID_STATE&&!s_usb.started&&s_usb.camera.phase==APP_CONSUMER_QUERY);return 0;}
 GrantUsb();VERIFY(s_usb.started);
 if(id==3){Push(APP_MESSAGE_STOP,2);Round(0);VERIFY(!s_usb.started&&!s_usb.stopping&&s_usb.camera.phase==APP_CONSUMER_RELEASE&&s_usb.camera.granted==1);return 0;}
 if(id==7){class_exists=false;configured=false;mode_state=USB_MODE_STATE_WAIT_CONFIGURED;Round(0);VERIFY(s_usb.camera.granted==1&&s_usb.camera.phase==APP_CONSUMER_GRANTED);return 0;}
 if(id==8){Loan();abort_result=SYS_ERR_IO;Push(APP_MESSAGE_STOP,2);Round(0);VERIFY(s_usb.stopping&&frame_owned&&s_usb.camera.phase==APP_CONSUMER_GRANTED);return 0;}
 VERIFY(0);return g_failure;
}

uint32_t Platform_SyncGetPrimask(void){return 0;}
uint32_t Platform_SyncGetBasepri(void){return 0;}
