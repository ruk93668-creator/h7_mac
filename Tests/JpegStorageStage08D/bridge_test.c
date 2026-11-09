//复用原28个租约用例，断言一字不改；只补上新增副本服务依赖
#define Stage02_TestRun LegacyBridge_TestRun
#include "../NativeRuntimeStage02/phase02c.c"
#undef Stage02_TestRun
#include "../../CM7/Application/app_frame_dispatcher.c"

static bool storage_wants;
static uint32_t copies;
static SysResult_t copy_result;
bool App_EventsIsOwner(RuntimeEventTarget_t target){return App_MessagesIsOwner(target);}
uint32_t Platform_TimeGetMs(void){return 100U;}
bool App_StorageWantsFrame(uint32_t now){(void)now;return storage_wants;}
SysResult_t App_StorageOfferFrame(const CameraFrame_t *frame,const AppFrameMeta_t *meta)
{
    (void)frame;(void)meta;copies++;storage_wants=false;return copy_result;
}

SysResult_t USB_UVC_SubmitFrameView(const VideoFrameView_t *view)
{
    return USB_UVC_SubmitFrame(view->spans[0].data,view->size_bytes);
}

int Stage02_TestRun(uint32_t id)
{
    CameraConfig_t cfg={160,120,CAMERA_PIXEL_FORMAT_YUV422};
    if(id<28)return LegacyBridge_TestRun(id);
    CHECK(App_UvcBridgeConfigure(&cfg)==SYS_OK);
    owner=RUNTIME_TARGET_CAMERA;CHECK(App_UvcBridgeCameraBegin()==SYS_OK);
    storage_wants=true;
    if(id==28){Cam(true);CHECK(copies==1&&acquired==1&&released==1);CHECK(counts[APP_CHANNEL_TO_USB]==0);CHECK(App_UvcBridgeCameraIsIdle());return 0;}
    if(id==29){release_result=SYS_ERR_IO;Cam(true);CHECK(s_camera_bridge.held&&s_camera_bridge.local_only&&released==0);release_result=SYS_OK;Cam(true);CHECK(released==1&&copies==1&&counts[APP_CHANNEL_TO_USB]==0);return 0;}
    if(id==30){copy_result=SYS_ERR_NO_RESOURCE;CHECK(Loan()==0);CHECK(copies==1&&usb_owned&&released==0);CHECK(Finish()==0);CHECK(released==1);return 0;}
    if(id==31){CHECK(Loan()==0);storage_wants=true;Cam(true);CHECK(acquired==1&&copies==1);CHECK(Finish()==0);CHECK(released==1);return 0;}
    if(id==32){Cam(false);CHECK(acquired==0&&copies==0);return 0;}
    if(id==33){full_channel=APP_CHANNEL_TO_USB;Usb(true);Cam(true);Cam(true);CHECK(acquired==1&&copies==1);full_channel=99;Usb(true);CHECK(submitted==0);Cam(true);Usb(true);CHECK(submitted==1&&usb_owned);return 0;}
    if(id==34){CHECK(Offer()==0);CHECK(queues[APP_CHANNEL_TO_USB][heads[APP_CHANNEL_TO_USB]].frame_id==1);CHECK(queues[APP_CHANNEL_TO_USB][heads[APP_CHANNEL_TO_USB]].timestamp_ms==100);return 0;}
    return __LINE__;
}
