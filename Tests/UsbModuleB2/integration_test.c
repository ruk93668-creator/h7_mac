//复用未修改HAL故障桩，执行真实BSP/Core/Mode/UVC及Task/Bridge
#include "../UvcStreaming/stream_test.c"
#include "../../CM7/Application/app_usb.c"
#include "../../CM7/Application/Tasks/usb_task.c"
#include "../../CM7/Application/app_uvc_bridge.c"

//固定消息通道只模拟运输，Frame Request/Offer/Return/ACK由真实Bridge处理
static AppMessage_t channels[APP_CHANNEL_COUNT][8];
static uint32_t heads[APP_CHANNEL_COUNT],counts[APP_CHANNEL_COUNT],full_channel=99;
static RuntimeEventTarget_t current_owner=RUNTIME_TARGET_USB;
static uint32_t acquired,released,bad_owner,notify_count,submitted_packets;
static bool media_loan,media_available=true;
static uint32_t frame_number;
static VideoConfig_t product={{160,120,CAMERA_PIXEL_FORMAT_YUV422},VIDEO_OUTPUT_USB_UVC,25,0};

//准备任务依赖但不运行真实RTOS调度，不访问相机或SD卡
bool App_MessagesIsOwner(RuntimeEventTarget_t t){return current_owner==t;}
bool App_EventsIsOwner(RuntimeEventTarget_t t){return current_owner==t;}
uint32_t Platform_SyncGetException(void){return __get_IPSR();}
uint32_t Platform_SyncGetPrimask(void){return __get_PRIMASK();}
uint32_t Platform_SyncGetBasepri(void){return __get_BASEPRI();}
void Platform_SyncDisableIrq(void){__disable_irq();}
void Platform_SyncRestoreIrq(uint32_t v){__set_PRIMASK(v);}
void Platform_SyncMemoryBarrier(void){__DMB();}
BaseType_t xTaskGetSchedulerState(void){return taskSCHEDULER_NOT_STARTED;}
TaskHandle_t xTaskCreateStatic(TaskFunction_t f,const char*n,const uint32_t w,void*a,UBaseType_t p,StackType_t*s,StaticTask_t*t)
{(void)f;(void)n;(void)w;(void)a;(void)p;(void)s;(void)t;return (TaskHandle_t)0x2003E000;}
SysResult_t App_EventsAttach(RuntimeEventTarget_t t,TaskHandle_t h){(void)t;(void)h;return SYS_OK;}
SysResult_t App_EventsWait(RuntimeEventTarget_t t,uint32_t ms,uint32_t*e){(void)t;(void)ms;*e=0;return SYS_ERR_TIMEOUT;}
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t t){(void)t;return 1800;}
void Platform_RtosAssert(const char*f,uint32_t l){(void)f;g_result.fail_line=l;}
SysResult_t App_StatusPublishUsb(const AppUsbStatus_t*s){(void)s;return SYS_OK;}

//消息满时不消费或覆盖原消息
bool App_MessagesReceive(AppChannel_t c,AppMessage_t*m){if(!counts[c])return false;*m=channels[c][heads[c]];heads[c]=(heads[c]+1)%8;counts[c]--;return true;}
SysResult_t App_MessagesSend(AppChannel_t c,const AppMessage_t*m)
{if(full_channel==c||counts[c]==8)return SYS_ERR_NO_RESOURCE;channels[c][(heads[c]+counts[c])%8]=*m;counts[c]++;return SYS_OK;}

//Media仅模拟一个合法原帧，归还只能由Camera执行
bool CameraCapture_StreamGetFrame(CameraFrame_t*f)
{
    if(current_owner!=RUNTIME_TARGET_CAMERA)bad_owner++;
    if(!media_available||media_loan)return false;
    memset(f,0,sizeof(*f));f->width=160;f->height=120;f->format=CAMERA_PIXEL_FORMAT_YUV422;
    f->data=s_frame;f->size_bytes=sizeof(s_frame);media_loan=true;media_available=false;acquired++;return true;
}
SysResult_t CameraCapture_StreamReleaseFrame(void)
{if(current_owner!=RUNTIME_TARGET_CAMERA)bad_owner++;if(!media_loan)return SYS_ERR_INVALID_STATE;media_loan=false;released++;return SYS_OK;}

//分发与Storage边界不参与本专项，不替换Bridge内部租约处理
SysResult_t App_FrameDispatcherValidateOutput(VideoOutput_t o){return o==VIDEO_OUTPUT_USB_UVC||o==VIDEO_OUTPUT_USB_TEST||o==VIDEO_OUTPUT_NONE?SYS_OK:SYS_ERR_NOT_SUPPORTED;}
SysResult_t App_FrameDispatcherBegin(uint32_t e){(void)e;return SYS_OK;}
bool App_FrameDispatcherCanAcquire(void){return true;}
bool App_FrameDispatcherNeedsStorage(void){return false;}
SysResult_t App_FrameDispatcherPublish(const CameraFrame_t*f,AppFrameMeta_t*m){m->frame_id=++frame_number;m->timestamp_ms=s_test_tick;m->stream_epoch=1;m->width=f->width;m->height=f->height;return SYS_OK;}
void App_StorageProcessReturns(void){}
const VideoConfig_t *VideoConfig_GetDefault(void){return &product;}

//只记录真实Core发出的通知，验证Setup及总线事件仍经过原通路
static void Notify(uint32_t e,void*p){(void)e;(void)p;notify_count++;}
static void Owner(RuntimeEventTarget_t t){current_owner=t;}
static void UsbRound(void){Owner(RUNTIME_TARGET_USB);UsbTask_Process(RUNTIME_EVENT_USB_PROGRESS);UsbTask_PublishStatus();}
static void CamRound(void){Owner(RUNTIME_TARGET_CAMERA);App_UvcBridgeCameraProcess(true);}
static void Command(AppMessageType_t type,uint32_t id)
{AppMessage_t m={.type=APP_MESSAGE_START};m.type=type;m.source=RUNTIME_TARGET_CONTROL;m.command_id=id;App_MessagesSend(APP_CHANNEL_USB_COMMAND,&m);}

//真实START经Mode连接，仍不代表已经配置或取流
static int Boot(UsbMode_t selected)
{
    VideoStreamConfig_t cfg={{160,120,CAMERA_PIXEL_FORMAT_YUV422},25,0};
    __HAL_RCC_USB_CONFIG(RCC_USBCLKSOURCE_HSI48);RCC->CR|=RCC_CR_HSI48RDY;
    EQ(USB_Device_SetEventCallback(Notify,NULL),SYS_OK);
    EQ(USB_Device_Init(),SYS_OK);
    if(selected==USB_MODE_UVC){EQ(USB_UVC_Configure(&cfg),SYS_OK);EQ(App_UvcBridgeConfigure(&cfg.camera),SYS_OK);}
    EQ(USB_Mode_SelectBootMode(selected,selected==USB_MODE_UVC?USB_UVC_Register:NULL),SYS_OK);
    EQ(USB_Mode_Init(),SYS_OK);
    App_USBSetDiagnosticObserver(UsbTask_Observe);
    Command(APP_MESSAGE_START,1);
    Owner(RUNTIME_TARGET_USB);UsbTask_Process(RUNTIME_EVENT_BOOT_READY);
    EQ(s_usb.started,true);EQ(s_usb.stopping,false);
    return 0;
}

//真实Bridge完成Request/Offer后，UVC仅登记借用，等待SOF发送
static int LoanFrame(void)
{
    Owner(RUNTIME_TARGET_CAMERA);EQ(App_UvcBridgeCameraBegin(),SYS_OK);
    UsbRound();CamRound();UsbRound();
    EQ(s_usb_bridge.state,BRIDGE_USB_LOANED);EQ(media_loan,true);EQ(s_uvc_payload.frame,s_frame);
    Tick();UsbRound();EQ(s_uvc_active->stream_state,USB_UVC_IN_FLIGHT);return 0;
}

//等待Camera完成归还及ACK后才能完成业务Stop
static int FinishStop(void)
{
    uint32_t n;
    for(n=0;n<4;n++){CamRound();UsbRound();}
    EQ(media_loan,false);EQ(s_usb_bridge.state,BRIDGE_USB_IDLE);
    EQ(s_usb.started,false);EQ(s_usb.stopping,false);EQ(bad_owner,0);return 0;
}

//每项从独立模拟器开始，覆盖真实异步状态和EP0同步路径
int B2_IntegrationRun(uint32_t id)
{
    uint32_t n,before,old_token;
    UsbUvcStreamStatus_t status;
    if(id==15){EQ(Boot(USB_MODE_TEST),0);HAL_PCD_ResetCallback(&s_pcd);Configured();UsbRound();EQ(USB_Device_IsConfigured(),true);EQ(s_uvc_active,NULL);Command(APP_MESSAGE_STOP,2);UsbRound();EQ(s_usb.started,false);return 0;}
    if(id==16){EQ(Boot(USB_MODE_NONE),0);EQ(start_count,0);EQ(s_usb_mode.state,USB_MODE_STATE_IDLE);Command(APP_MESSAGE_STOP,2);UsbRound();EQ(s_usb.started,false);EQ(start_count,0);return 0;}
    EQ(Boot(USB_MODE_UVC),0);
    EQ(s_usb_mode.state,USB_MODE_STATE_WAIT_CONFIGURED);EQ(USB_Device_IsConfigured(),false);
    if(id==0){Command(APP_MESSAGE_STOP,2);UsbRound();EQ(s_usb.started,false);EQ(s_usb_mode.state,USB_MODE_STATE_IDLE);return 0;}
    if(id==18){s_test_tick=5000;UsbRound();EQ(s_usb.fault,SYS_ERR_TIMEOUT);EQ(g_usb_task_debug.result,SYS_ERR_TIMEOUT);EQ(s_usb.started,false);return 0;}
    HAL_PCD_ResetCallback(&s_pcd);Configured();UsbRound();
    if(id==1){EQ(acquired,0);EQ(s_uvc_active->commit_valid,false);UsbRound();EQ(acquired,0);EQ(Ep1_Commit(),0);UsbRound();EQ(acquired,0);EQ(s_uvc_active->interface_alt[1],0);return 0;}
    EQ(Ep1_Commit(),0);EQ(Ep1_Open(),0);
    if(id==9||id==10){
        before=notify_count;
        if(id==10){Request(0x81,USB_REQ_GET_INTERFACE,0,3,1);EQ(stall_count>0,true);}
        Request(0x80,USB_REQ_GET_DESCRIPTOR,0x100,0,18);
        EQ(tx_size,18);EQ(stall_count,0);EQ(s_device.ep0_state,USBD_EP0_DATA_IN);
        HAL_PCD_DataInStageCallback(&s_pcd,0);HAL_PCD_DataOutStageCallback(&s_pcd,0);
        EQ(notify_count>before,true);EQ(s_endpoints[1][0].busy,false);return 0;
    }
    if(id==17){Command(APP_MESSAGE_START,2);UsbRound();EQ(s_usb.fault,SYS_ERR_BUSY);EQ(s_usb.started,true);EQ(s_usb.stopping,false);return 0;}
    EQ(LoanFrame(),0);
    if(id==2){
        n=0;
        while(s_uvc_payload.frame!=NULL&&n<100){
            if(s_uvc_active->stream_state==USB_UVC_IN_FLIGHT){submitted_packets++;Done();UsbRound();}
            else{Tick();UsbRound();}
            n++;
        }
        EQ(s_uvc_payload.frame,NULL);EQ(USB_UVC_GetCompletedFrameCount(),1);
        EQ(submitted_packets,(sizeof(s_frame)+UVC_VIDEO_PACKET_BYTES-3)/(UVC_VIDEO_PACKET_BYTES-2));
        EQ(media_loan,true);CamRound();UsbRound();EQ(media_loan,false);EQ(released,1);return 0;
    }
    if(id==8){before=s_uvc_payload.frame_offset;old_token=s_uvc_active->packet.token;Done();EQ(s_uvc_payload.frame_offset,before);EQ(s_uvc_active->event_token,old_token);UsbRound();EQ(s_uvc_payload.frame_offset,UVC_VIDEO_PACKET_BYTES-2);return 0;}
    if(id==5||id==6){
        if(id==5)HAL_PCD_ResetCallback(&s_pcd);else HAL_PCD_DisconnectCallback(&s_pcd);
        EQ(media_loan,true);EQ(s_usb.started,true);
        UsbRound();CamRound();UsbRound();EQ(media_loan,false);EQ(released,1);
        EQ(s_usb.started,true);Command(APP_MESSAGE_STOP,2);UsbRound();EQ(s_usb.started,false);return 0;
    }
    if(id==7){
        HAL_PCD_SuspendCallback(&s_pcd);UsbRound();EQ(USB_Device_IsConfigured(),false);
        EQ(s_uvc_payload.frame,s_frame);EQ(media_loan,true);EQ(s_usb.started,true);EQ(s_usb.stopping,false);
        HAL_PCD_ResumeCallback(&s_pcd);UsbRound();EQ(USB_Device_IsConfigured(),true);
    }
    if(id==11){fault=20;HAL_PCD_ISOINIncompleteCallback(&s_pcd,1);UsbRound();EQ(s_uvc_payload.frame,s_frame);EQ(media_loan,true);fault=0;}
    if(id==4)fault=20;
    Command(APP_MESSAGE_STOP,2);before=stop_count;UsbRound();
    if(id==4){EQ(USB_UVC_GetStreamStatus(&status),SYS_OK);EQ(status.frame_owned,true);EQ(s_usb.stopping,true);EQ(s_usb.stop_command,false);EQ(stop_count,before);EQ(media_loan,true);fault=0;UsbRound();}
    EQ(s_usb.stopping,true);EQ(media_loan,true);EQ(stop_count,before);
    if(id==12)full_channel=APP_CHANNEL_TO_USB;
    CamRound();
    if(id==12){EQ(media_loan,false);UsbRound();EQ(s_usb.stopping,true);EQ(stop_count,before);full_channel=99;CamRound();}
    if(id==13){fault=8;UsbRound();EQ(s_usb.stopping,true);EQ(s_usb.started,true);EQ(g_usb_task_debug.result,SYS_ERR_IO);fault=0;}
    EQ(FinishStop(),0);
    EQ(released,1);
    if(id==14){Command(APP_MESSAGE_STOP,3);UsbRound();EQ(s_usb.started,false);EQ(s_usb.stopping,false);EQ(released,1);}
    return 0;
}
