#include "../../CM7/Application/app_uvc_bridge.c"
static RuntimeEventTarget_t owner;
static AppMessage_t queues[APP_CHANNEL_COUNT][4];
static uint32_t counts[APP_CHANNEL_COUNT],heads[APP_CHANNEL_COUNT],full_channel=99;
static uint32_t acquired,released,submitted,wrong_owner;
static bool ready=true,media_available=true,usb_owned,class_exists=true;
static SysResult_t submit_result,release_result,status_result;
static uint8_t image[38400];
void *memset(void *d,int v,size_t n){uint8_t *p=d;while(n--)*p++=(uint8_t)v;return d;}
void *memcpy(void *d,const void *s,size_t n){uint8_t *p=d;const uint8_t *q=s;while(n--)*p++=*q++;return d;}
void __aeabi_memclr4(void *d,size_t n){volatile uint8_t *p=d;while(n--)*p++=0;}
void __aeabi_memclr(void *d,size_t n){volatile uint8_t *p=d;while(n--)*p++=0;}
void __aeabi_memcpy(void *d,const void *s,size_t n){volatile uint8_t *p=d;const volatile uint8_t *q=s;while(n--)*p++=*q++;}
void __aeabi_memcpy4(void *d,const void *s,size_t n){volatile uint8_t *p=d;const volatile uint8_t *q=s;while(n--)*p++=*q++;}
bool App_MessagesIsOwner(RuntimeEventTarget_t t){return owner==t;}
SysResult_t App_MessagesSend(AppChannel_t c,const AppMessage_t *m){if(full_channel==(uint32_t)c||counts[c]==4)return SYS_ERR_NO_RESOURCE;queues[c][(heads[c]+counts[c])%4]=*m;counts[c]++;return SYS_OK;}
bool App_MessagesReceive(AppChannel_t c,AppMessage_t *m){if(!counts[c])return false;*m=queues[c][heads[c]];heads[c]=(heads[c]+1)%4;counts[c]--;return true;}
bool CameraCapture_StreamGetFrame(CameraFrame_t *f){if(owner!=RUNTIME_TARGET_CAMERA)wrong_owner++;if(!media_available)return false;media_available=false;acquired++;f->width=160;f->height=120;f->format=CAMERA_PIXEL_FORMAT_YUV422;f->size_bytes=38400;f->data=image;return true;}
SysResult_t CameraCapture_StreamReleaseFrame(void){if(owner!=RUNTIME_TARGET_CAMERA)wrong_owner++;if(release_result!=SYS_OK)return release_result;released++;media_available=true;return SYS_OK;}
SysResult_t USB_UVC_GetStreamStatus(UsbUvcStreamStatus_t *s){if(owner!=RUNTIME_TARGET_USB)wrong_owner++;if(!class_exists)return SYS_ERR_INVALID_STATE;if(status_result!=SYS_OK)return status_result;memset(s,0,sizeof(*s));s->frame_owned=usb_owned;s->can_submit=ready&&!usb_owned;return SYS_OK;}
SysResult_t USB_UVC_SubmitFrame(const uint8_t *p,uint32_t n){(void)p;(void)n;if(owner!=RUNTIME_TARGET_USB)wrong_owner++;if(submit_result!=SYS_OK)return submit_result;submitted++;usb_owned=true;return SYS_OK;}
SysResult_t USB_UVC_CancelFrame(void){return SYS_OK;}
const VideoConfig_t *VideoConfig_GetDefault(void){return NULL;}
SysResult_t USB_UVC_ValidateConfig(const VideoStreamConfig_t *c){(void)c;return SYS_OK;}
SysResult_t USB_UVC_Configure(const VideoStreamConfig_t *c){(void)c;return SYS_OK;}
SysResult_t USB_UVC_Register(void){return SYS_OK;}
SysResult_t USB_Mode_SelectBootMode(UsbMode_t m,SysResult_t (*r)(void)){(void)m;(void)r;return SYS_OK;}
#define CHECK(x) do{if(!(x))return __LINE__;}while(0)
static void Cam(bool enabled){owner=RUNTIME_TARGET_CAMERA;App_UvcBridgeCameraProcess(enabled);}
static void Usb(bool enabled){owner=RUNTIME_TARGET_USB;App_UvcBridgeUsbProcess(enabled);}
static int Offer(void){Usb(true);CHECK(s_usb_bridge.state==BRIDGE_USB_REQUEST);Cam(true);CHECK(acquired==1);return 0;}
static int Loan(void){CHECK(Offer()==0);Usb(true);CHECK(usb_owned);CHECK(submitted==1);return 0;}
static int Finish(void){usb_owned=false;Usb(false);CHECK(released==0);Cam(false);CHECK(released==1);Usb(false);CHECK(App_UvcBridgeUsbIsIdle());return 0;}
int Stage02_TestRun(uint32_t id)
{
    CameraConfig_t cfg={160,120,CAMERA_PIXEL_FORMAT_YUV422};AppMessage_t old,msg;uint32_t i;
    CHECK(App_UvcBridgeConfigure(&cfg)==SYS_OK);owner=RUNTIME_TARGET_CAMERA;CHECK(App_UvcBridgeCameraBegin()==SYS_OK);
    if(id==0){ready=false;Usb(true);CHECK(counts[APP_CHANNEL_TO_CAMERA]==0);return 0;}
    if(id==1){owner=RUNTIME_TARGET_USB;CHECK(App_UvcBridgeCameraBegin()==SYS_ERR_INVALID_STATE);App_UvcBridgeCameraProcess(true);CHECK(acquired==0);return 0;}
    if(id==6){full_channel=APP_CHANNEL_TO_CAMERA;Usb(true);CHECK(!s_usb_bridge.request_sent);CHECK(acquired==0);full_channel=99;Usb(true);Cam(true);CHECK(acquired==1);return 0;}
    if(id==7){full_channel=APP_CHANNEL_TO_USB;Usb(true);Cam(true);Cam(true);CHECK(acquired==1&&s_camera_bridge.held);full_channel=99;Cam(true);Usb(true);CHECK(submitted==1);return 0;}
    if(id==20){media_available=false;Usb(true);Cam(true);Usb(false);CHECK(s_usb_bridge.state==BRIDGE_USB_REQUEST);Cam(false);Usb(false);CHECK(App_UvcBridgeUsbIsIdle());CHECK(released==0);return 0;}
    if(id==22){s_camera_bridge.next_lease=UINT32_MAX;Usb(true);Cam(true);CHECK(acquired==0);Usb(true);CHECK(s_usb_bridge.state==BRIDGE_USB_IDLE);return 0;}
    if(id==23){s_camera_bridge.epoch=UINT32_MAX;CHECK(App_UvcBridgeCameraBegin()==SYS_ERR_NO_RESOURCE);return 0;}
    if(id==24){s_usb_bridge.next_request=UINT32_MAX;Usb(true);CHECK(counts[APP_CHANNEL_TO_CAMERA]==0);return 0;}
    if(id==14||id==15){CHECK(Offer()==0);if(id==14)queues[APP_CHANNEL_TO_USB][heads[APP_CHANNEL_TO_USB]].frame.size_bytes=1;else submit_result=SYS_ERR_BUSY;Usb(true);CHECK(!usb_owned&&submitted==0);Cam(true);Usb(false);CHECK(released==1);return 0;}
    if(id==25){CHECK(Offer()==0);queues[APP_CHANNEL_TO_USB][heads[APP_CHANNEL_TO_USB]].stream_epoch=0;Usb(true);CHECK(released==0&&submitted==0);return 0;}
    CHECK(Loan()==0);old=s_usb_bridge.lease;
    if(id==2)return 0;
    if(id==3){for(i=0;i<20;i++){Cam(true);Usb(true);}CHECK(acquired==1&&released==0);return 0;}
    if(id==5){usb_owned=false;Usb(false);release_result=SYS_ERR_IO;Cam(false);CHECK(released==0&&s_camera_bridge.held);CHECK(counts[APP_CHANNEL_TO_USB]==0);release_result=SYS_OK;Cam(false);CHECK(released==1);Usb(false);CHECK(App_UvcBridgeUsbIsIdle());return 0;}
    if(id==8){usb_owned=false;full_channel=APP_CHANNEL_TO_CAMERA;Usb(false);CHECK(!s_usb_bridge.return_sent&&released==0);full_channel=99;Usb(false);Cam(false);Usb(false);CHECK(released==1);return 0;}
    if(id==9){usb_owned=false;Usb(false);full_channel=APP_CHANNEL_TO_USB;Cam(false);Cam(false);CHECK(released==1&&s_camera_bridge.ack_pending);CHECK(s_usb_bridge.state==BRIDGE_USB_RETURN);full_channel=99;Cam(false);Usb(false);CHECK(App_UvcBridgeUsbIsIdle());return 0;}
    if(id==11||id==12){msg=old;msg.type=APP_MESSAGE_FRAME_RETURN;if(id==11)msg.lease_id++;else msg.stream_epoch++;App_MessagesSend(APP_CHANNEL_TO_CAMERA,&msg);Cam(true);CHECK(released==0&&s_camera_bridge.held);return 0;}
    if(id==13){usb_owned=false;Usb(false);msg=old;msg.type=APP_MESSAGE_RETURN_ACK;msg.stream_epoch++;App_MessagesSend(APP_CHANNEL_TO_USB,&msg);Usb(false);CHECK(s_usb_bridge.state==BRIDGE_USB_RETURN);CHECK(released==0);return 0;}
    if(id==16){class_exists=false;usb_owned=false;Usb(false);Cam(false);Usb(false);CHECK(released==1);return 0;}
    if(id==17){status_result=SYS_ERR_IO;Usb(false);CHECK(released==0&&s_usb_bridge.state==BRIDGE_USB_LOANED);return 0;}
    if(id==18){Usb(false);Cam(false);CHECK(usb_owned&&released==0&&s_usb_bridge.state==BRIDGE_USB_LOANED);return 0;}
    if(id==19){Usb(false);CHECK(released==0);usb_owned=false;Usb(false);Cam(false);Usb(false);CHECK(released==1);return 0;}
    if(id==26){App_MessagesSend(APP_CHANNEL_TO_USB,&old);Usb(true);CHECK(submitted==1&&released==0);return 0;}
    CHECK(Finish()==0);
    if(id==10){old.type=APP_MESSAGE_FRAME_RETURN;App_MessagesSend(APP_CHANNEL_TO_CAMERA,&old);Cam(false);CHECK(released==1);return 0;}
    if(id==21){owner=RUNTIME_TARGET_CAMERA;CHECK(App_UvcBridgeCameraBegin()==SYS_OK);Usb(true);Cam(true);Usb(true);old.type=APP_MESSAGE_FRAME_RETURN;App_MessagesSend(APP_CHANNEL_TO_CAMERA,&old);Cam(true);CHECK(released==1&&s_camera_bridge.held);CHECK(s_camera_bridge.epoch==2);return 0;}
    if(id==27){for(i=0;i<1000;i++){Usb(true);Cam(true);Usb(true);CHECK(usb_owned);usb_owned=false;Usb(false);Cam(false);Usb(false);}CHECK(acquired==1001&&released==1001&&wrong_owner==0);return 0;}
    CHECK(wrong_owner==0);return 0;
}
