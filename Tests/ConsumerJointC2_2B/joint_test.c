//执行真实Task、Camera、Bridge、Dispatcher、Storage及JPEG/AVI，硬件和文件介质使用桩
#include "runtime_event.h"
static RuntimeEventTarget_t integration_owner=RUNTIME_TARGET_CAMERA;
static unsigned integration_usb_ref, integration_sd_ref;
#define B1_INTEGRATION
#include "../CameraModuleB1/behavior_test.c"
#include "../../CM7/Application/app_uvc_bridge.c"
#include "../../CM7/Application/app_frame_dispatcher.c"
#include "../../CM7/Application/app_storage.c"
#include "../../CM7/Service/jpeg_frame.c"
#include "../../CM7/Service/mjpeg_avi.c"
#include "jpeg_data.h"
#include "../../CM7/Application/app_usb.c"

//保存介质和通道桩，禁止测试调用真实SD卡
static AppMessage_t channels[APP_CHANNEL_COUNT][8];
static unsigned channel_heads[APP_CHANNEL_COUNT],channel_counts[APP_CHANNEL_COUNT];
static uint32_t irq_mask,media_releases,sd_releases,bad_owner,lease_retries,full_channel=99;
static bool frame_available=true,usb_owned;
static SysResult_t fs_write_result,lease_result,cancel_result;
static uint8_t file_data[32768];
static uint32_t file_position,file_size,syncs,closes;
static bool mounted;
#define CHECK(x) do{if(!(x))return __LINE__;}while(0)

//只切换测试执行者，不替代生产任务所有权检查
static void Owner(RuntimeEventTarget_t target){integration_owner=target;}
bool App_EventsIsOwner(RuntimeEventTarget_t target){return integration_owner==target;}
uint32_t Platform_SyncGetPrimask(void){return irq_mask;}
uint32_t Platform_SyncGetBasepri(void){return 0;}
void Platform_SyncDisableIrq(void){irq_mask=1;}
void Platform_SyncRestoreIrq(uint32_t saved){irq_mask=saved;}
void Platform_SyncMemoryBarrier(void){}
SysResult_t RuntimeEvent_Post(RuntimeEventTarget_t target,uint32_t events){(void)target;(void)events;return SYS_OK;}

//模拟非阻塞通道满，不丢弃已排队的凭据
bool App_MessagesReceive(AppChannel_t c,AppMessage_t*m)
{
    if(c==APP_CHANNEL_CAMERA_COMMAND){if(!queue_count)return false;*m=queue[received++];queue_count--;return true;}
    if(!channel_counts[c])return false;
    *m=channels[c][channel_heads[c]];channel_heads[c]=(channel_heads[c]+1)%8;channel_counts[c]--;return true;
}
SysResult_t App_MessagesSend(AppChannel_t c,const AppMessage_t*m)
{
    if(full_channel==c || channel_counts[c]==8)return SYS_ERR_NO_RESOURCE;
    channels[c][(channel_heads[c]+channel_counts[c])%8]=*m;channel_counts[c]++;return SYS_OK;
}
SysResult_t App_StatusPublishCamera(const AppCameraStatus_t*s){(void)s;return SYS_OK;}

//以真实JPEG样本借出原帧，SD和USB拥有独立引用
bool CameraCapture_StreamGetFrame(CameraFrame_t*f)
{
    if(integration_owner!=RUNTIME_TARGET_CAMERA)bad_owner++;
    if(!frame_available || integration_usb_ref)return false;
    memset(f,0,sizeof(*f));f->width=160;f->height=120;f->format=CAMERA_PIXEL_FORMAT_JPEG;
    f->data=jpeg_data;f->size_bytes=sizeof(jpeg_data);
    if(case_id==1){f->size_bytes2=sizeof(jpeg_data)-600;f->data2=jpeg_data+600;}
    integration_usb_ref=1;frame_available=false;return true;
}
SysResult_t CameraCapture_StreamReleaseFrame(void)
{
    if(integration_owner!=RUNTIME_TARGET_CAMERA)bad_owner++;
    if(!integration_usb_ref)return SYS_ERR_INVALID_STATE;
    integration_usb_ref=0;media_releases++;return SYS_OK;
}
SysResult_t CameraMjpegFifo_RetainFrame(const CameraFrame_t*f,CameraMjpegLease_t*l)
{(void)f;if(integration_owner!=RUNTIME_TARGET_CAMERA)bad_owner++;if(integration_sd_ref)return SYS_ERR_BUSY;integration_sd_ref=1;l->session=1;l->id=1;return SYS_OK;}
SysResult_t CameraMjpegFifo_ReleaseLease(const CameraMjpegLease_t*l)
{(void)l;if(integration_owner!=RUNTIME_TARGET_CAMERA)bad_owner++;if(lease_result!=SYS_OK){lease_retries++;return lease_result;}if(!integration_sd_ref)return SYS_ERR_INVALID_STATE;integration_sd_ref=0;sd_releases++;return SYS_OK;}

//UVC桩区分成功借用、取消失败及真实归还
SysResult_t USB_UVC_GetStreamStatus(UsbUvcStreamStatus_t*s){memset(s,0,sizeof(*s));s->can_submit=!usb_owned;s->frame_owned=usb_owned;s->state=cancel_result==SYS_OK?USB_UVC_WAIT_FRAME:USB_UVC_RECOVERY;s->result=cancel_result;return SYS_OK;}
SysResult_t USB_UVC_SubmitFrame(const uint8_t*p,uint32_t n){(void)p;(void)n;usb_owned=true;return SYS_OK;}
SysResult_t USB_UVC_SubmitFrameView(const VideoFrameView_t*v){CHECK(v->span_count==2);usb_owned=true;return SYS_OK;}
SysResult_t USB_UVC_CancelFrame(void){if(cancel_result==SYS_OK)usb_owned=false;return cancel_result;}
SysResult_t USB_UVC_ValidateConfig(const VideoStreamConfig_t*c){(void)c;return SYS_OK;}
SysResult_t USB_UVC_Configure(const VideoStreamConfig_t*c){(void)c;return SYS_OK;}
SysResult_t USB_UVC_Register(void){return SYS_OK;}
SysResult_t USB_Mode_SelectBootMode(UsbMode_t m,SysResult_t(*f)(void)){(void)m;(void)f;return SYS_OK;}

//内存文件实现保留实际AVI/JPEG字节，仅用于验证上层生命周期
SysResult_t StorageFS_Mount(bool w,uint8_t p){(void)w;(void)p;if(integration_owner!=RUNTIME_TARGET_STORAGE)bad_owner++;mounted=true;return SYS_OK;}
SysResult_t StorageFS_GetSpace(uint64_t*t,uint64_t*f){*t=10000000;*f=9000000;return SYS_OK;}
SysResult_t StorageFS_Unmount(void){mounted=false;return SYS_OK;}
SysResult_t StorageFS_Recover(void){mounted=false;return SYS_OK;}
SysResult_t StorageFS_Open(const char*n,uint32_t m,StorageFile_t*f){(void)n;(void)m;*f=1;file_position=0;file_size=0;return SYS_OK;}
SysResult_t StorageFS_Write(StorageFile_t f,const void*d,uint32_t n,uint32_t*w)
{(void)f;if(integration_owner!=RUNTIME_TARGET_STORAGE)bad_owner++;*w=0;if(fs_write_result!=SYS_OK)return fs_write_result;if(file_position+n>sizeof(file_data))return SYS_ERR_NO_RESOURCE;memcpy(file_data+file_position,d,n);file_position+=n;if(file_size<file_position)file_size=file_position;*w=n;return SYS_OK;}
SysResult_t StorageFS_Read(StorageFile_t f,void*d,uint32_t n,uint32_t*r){(void)f;if(n>file_size-file_position)n=file_size-file_position;memcpy(d,file_data+file_position,n);file_position+=n;*r=n;return SYS_OK;}
SysResult_t StorageFS_Seek(StorageFile_t f,uint32_t o){(void)f;file_position=o;return SYS_OK;}
SysResult_t StorageFS_GetSize(StorageFile_t f,uint32_t*n){(void)f;*n=file_size;return SYS_OK;}
SysResult_t StorageFS_Sync(StorageFile_t f){(void)f;syncs++;return SYS_OK;}
SysResult_t StorageFS_Close(StorageFile_t f){(void)f;closes++;return SYS_OK;}
SysResult_t StorageFS_GetDiagnostics(StorageFsDiagnostics_t*d){memset(d,0,sizeof(*d));d->state=mounted?2:1;return SYS_OK;}


//测试Mode与UVC只模拟物理侧状态，不代替App_USB消费者或帧协议
static UsbModeState_t joint_mode=USB_MODE_STATE_IDLE;
static uint32_t joint_eof;
UsbMode_t USB_Mode_GetMode(void){return USB_MODE_UVC;}
SysResult_t USB_Mode_Start(void){joint_mode=USB_MODE_STATE_RUNNING;return SYS_OK;}
void USB_Mode_Process(void){}
SysResult_t USB_Mode_GetLastResult(void){return joint_mode==USB_MODE_STATE_RUNNING||joint_mode==USB_MODE_STATE_IDLE?SYS_OK:SYS_ERR_IO;}
SysResult_t USB_Mode_Stop(void){joint_mode=USB_MODE_STATE_IDLE;return SYS_OK;}
uint32_t USB_Device_GetSofSequence(void){return tick;}
uint32_t USB_UVC_GetCompletedFrameCount(void){return joint_eof;}
void USB_UVC_Process(void){}
//运输复用既有测试有界队列，资格与Replay仍由真实Task/模块推进
#include "joint_transport.h"
//当前Task唯一所有者轮次，保留ACK交付和原Begin/Media/Bridge/Finish顺序
static void Joint_Camera(void){Owner(RUNTIME_TARGET_CAMERA);CameraTask_Process(RUNTIME_EVENT_BOOT_READY);}
//真实USB业务推进；底层UVC为允许的硬件桩
static void Joint_Usb(void)
{
 AppLifecycleMessage_t m;AppMessage_t out;
 Owner(RUNTIME_TARGET_USB);
 while(App_LifecycleMessagesReceive(APP_LIFECYCLE_CHANNEL_USB,&m))(void)App_USBReceiveLifecycle(&m);
 App_USBProcess();
 if(App_USBIsStopping()){(void)USB_UVC_CancelFrame();USB_UVC_Process();App_UvcBridgeUsbProcess(false);USB_UVC_Process();}
 else{USB_UVC_Process();App_UvcBridgeUsbProcess(App_USBCanTransfer());}
 App_USBFinishRound(true);
 if(App_USBGetPendingAck(&out)&&App_MessagesSend(APP_CHANNEL_CONTROL,&out)==SYS_OK)App_USBConfirmAckEnqueued();
 if(App_USBGetPendingFault(&out)&&App_MessagesSend(APP_CHANNEL_CONTROL,&out)==SYS_OK)App_USBConfirmFaultEnqueued();
 (void)App_USBFlushLifecycle(Joint_Send,0);
}
//Storage只在真实授权后挂载和创建文件，Camera仍负责实际归还
static void Joint_Storage(void)
{
 AppLifecycleMessage_t m;Owner(RUNTIME_TARGET_STORAGE);
 while(App_LifecycleMessagesReceive(APP_LIFECYCLE_CHANNEL_STORAGE,&m))(void)App_StorageReceiveLifecycle(&m);
 App_StorageProcess();(void)App_StorageFlushLifecycle(Joint_Send,0);
}
//唯一新增测试入口，不复用旧Camera同步START或STOP
int Joint_Run(uint32_t id)
{
 AppStorageConfig_t sc={25,131072,10000,1000000,0,10,0};
 AppMessage_t command={.type=APP_MESSAGE_START,.source=RUNTIME_TARGET_CONTROL,.command_id=10},saved;
 AppCameraConsumerStatus_t us,sd;AppUsbRuntimeStatus_t usbstatus;AppQualificationStatus_t q;uint32_t i,ack_seen=0;
 case_id=id;config.camera.format=CAMERA_PIXEL_FORMAT_JPEG;frame_available=false;sensor.base.state=DEVICE_STATE_READY;
 CHECK(App_CameraConfigure(&config.camera)==SYS_OK);
 CHECK(App_UvcBridgeConfigure(&config.camera)==SYS_OK);
 Owner(RUNTIME_TARGET_STORAGE);CHECK(App_StoragePrepare(&sc)==SYS_OK);
 Owner(RUNTIME_TARGET_USB);App_USBHandleCommand(&command);
 for(i=0;i<8;i++){Joint_Usb();Joint_Camera();}
 App_USBGetLifecycleStatus(&us);CHECK(us.granted==1);CHECK(App_USBCanTransfer());
 Owner(RUNTIME_TARGET_STORAGE);CHECK(App_StorageStartRecording()==SYS_OK);
 CHECK(!mounted);
 for(i=0;i<8;i++){Joint_Storage();Joint_Camera();}
 App_StorageGetLifecycleStatus(&sd);CHECK(sd.granted==1&&s_state==STORAGE_VIDEO&&mounted);
 frame_available=true;Joint_Usb();
 if(id==2)full_channel=APP_CHANNEL_TO_USB;
 Joint_Camera();
 if(id==2){CHECK(integration_usb_ref&&integration_sd_ref);full_channel=99;Joint_Camera();CHECK(g_bridge_debug.acquired_frames==1);}
 Joint_Usb();CHECK(usb_owned&&integration_usb_ref&&integration_sd_ref);
 if(id==3){Owner(RUNTIME_TARGET_CAMERA);CHECK(!App_StorageWantsFrame(tick));CHECK(g_storage_status.queue_high_water==1);}
 if(id==4)fs_write_result=SYS_ERR_IO;
 if(id==5)lease_result=SYS_ERR_IO;
 if(id==6){Owner(RUNTIME_TARGET_CAMERA);Push(APP_MESSAGE_STOP,11);Joint_Camera();CHECK(!s_camera.stopping);App_USBGetLifecycleStatus(&us);App_StorageGetLifecycleStatus(&sd);CHECK(us.granted==1&&sd.granted==1);CHECK(!stopped);for(i=0;i<channel_counts[APP_CHANNEL_CONTROL];i++){AppMessage_t *a=&channels[APP_CHANNEL_CONTROL][(channel_heads[APP_CHANNEL_CONTROL]+i)%8];if(a->type==APP_MESSAGE_ACK&&a->source==RUNTIME_TARGET_CAMERA&&a->command_id==11){CHECK(a->result==SYS_ERR_NOT_SUPPORTED);ack_seen++;}}CHECK(ack_seen==1);}
 Joint_Storage();CHECK(g_storage_status.pending_returns==1&&integration_sd_ref);
 if(id==4)CHECK(s_state==STORAGE_FAULT&&g_storage_status.saved_frames==0);else CHECK(g_storage_status.saved_frames==1);
 Joint_Camera();
 if(id==5){CHECK(integration_sd_ref);lease_result=SYS_OK;Joint_Camera();CHECK(lease_retries>0);}
 CHECK(!integration_sd_ref&&sd_releases==1&&integration_usb_ref);
 //USB通过真实业务STOP驱动取消及归还；失败仍保留原帧
 Owner(RUNTIME_TARGET_USB);command.type=APP_MESSAGE_STOP;command.command_id=12;App_USBHandleCommand(&command);
 if(id==7)cancel_result=SYS_ERR_IO;
 Joint_Usb();
 if(id==7){CHECK(usb_owned&&integration_usb_ref);cancel_result=SYS_OK;Joint_Usb();}
 CHECK(channel_counts[APP_CHANNEL_TO_CAMERA]>0);saved=channels[APP_CHANNEL_TO_CAMERA][channel_heads[APP_CHANNEL_TO_CAMERA]];
 if(id==8)full_channel=APP_CHANNEL_TO_USB;
 Joint_Camera();CHECK(!integration_usb_ref&&media_releases==1);
 if(id==8){CHECK(s_camera_bridge.ack_pending);full_channel=99;Joint_Camera();}
 Joint_Usb();
 if(id==9){CHECK(App_MessagesSend(APP_CHANNEL_TO_CAMERA,&saved)==SYS_OK);Joint_Camera();Joint_Usb();CHECK(media_releases==1);}
 if(id==10){saved.lease_id++;CHECK(App_MessagesSend(APP_CHANNEL_TO_CAMERA,&saved)==SYS_OK);Joint_Camera();Joint_Usb();CHECK(media_releases==1);}
 CHECK(!integration_sd_ref&&!integration_usb_ref&&bad_owner==0);
 if(id!=4){Owner(RUNTIME_TARGET_STORAGE);CHECK(App_StorageStop()==SYS_OK);Joint_Storage();CHECK(s_state==STORAGE_IDLE&&closes==1&&syncs>=1);CHECK(!memcmp(file_data,"RIFF",4));}
 App_USBGetStatus(&usbstatus);CHECK(usbstatus.state==0);CHECK(s_camera.running&&!stopped);
 //真实适配器没有released/idle证明；业务停止后资格必须继续保留
 for(i=0;i<4;i++){Joint_Camera();Joint_Usb();Joint_Storage();}
 Owner(RUNTIME_TARGET_CAMERA);CHECK(App_CameraQualificationGetStatus(APP_CONSUMER_USB,&q)==SYS_OK);CHECK(q.phase==APP_QUAL_RELEASING);
 App_USBGetLifecycleStatus(&us);App_StorageGetLifecycleStatus(&sd);CHECK(us.granted==1&&sd.granted==1);CHECK(s_camera.running&&!stopped);
 return 0;
}
//仅供仿真器运行真实CameraTask初始化，首次等待处交回测试调度器
void Joint_PrepareTask(void){CameraTask_Run(0);}
