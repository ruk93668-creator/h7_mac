#include "stm32h7xx.h"
static uint32_t test_ipsr;
#define __get_IPSR() test_ipsr
#include "../UvcStreaming/stream_test.c"
#include "../../Middlewares/Third_Party/FreeRTOS/Source/tasks.c"
#include "../../CM7/Platform/platform_rtos.c"
#include "../../Common/Src/runtime_event.c"
#include "../../CM7/Media/Src/camera_capture.c"
#include "../../CM7/Device/Src/device.c"
#define s_initialized app_events_initialized
#include "../../CM7/Application/app_events.c"
#undef s_initialized
#include "../../CM7/Application/app_messages.c"
#include "../../CM7/Application/app_camera.c"
#include "../../CM7/Application/app_uvc_bridge.c"
#include "../../CM7/Application/Tasks/camera_task.c"
#include "../../CM7/Application/Tasks/usb_task.c"
#include "../../CM7/Application/Tasks/control_task.c"
#include "../../CM7/Application/app_tasks.c"
#include "../../CM7/Application/app_main.c"
#include "../../CM7/Application/video_config.c"

int strcmp(const char *a,const char *b){while(*a&&*a==*b){a++;b++;}return (unsigned char)*a-(unsigned char)*b;}
uint32_t SystemCoreClock=480000000U;
static BspDcmiFrameCallback_t camera_frame_callback;
static void *camera_context;
static BspDcmiEventCallback_t camera_error_callback;
static void *camera_error_context;
static uint32_t *dma_targets[2];
static bool fake_dma_active;
static BspDcmiEvents_t fake_dcmi_events;
static SysResult_t fake_camera_start,fake_dcmi_stop;
static uint32_t sensor_starts,sensor_stops,dcmi_starts;
static SysResult_t ConfigureSensor(Ov5640Device_t *p,const CameraConfig_t *c){(void)p;return Ov5640_ValidateConfig(c);}
static const Ov5640Ops_t camera_ops={NULL,ConfigureSensor};
static SysResult_t SensorInit(Device_t *d){(void)d;return SYS_OK;}
static SysResult_t SensorStart(Device_t *d){(void)d;if(fake_camera_start!=SYS_OK)return fake_camera_start;sensor_starts++;return SYS_OK;}
static SysResult_t SensorStop(Device_t *d){(void)d;if(fake_dma_active||s_camera_bridge.held)return SYS_ERR_BUSY;sensor_stops++;return SYS_OK;}
static const DeviceLifecycleOps_t sensor_lifecycle={SensorInit,SensorStart,SensorStop};
static Ov5640Device_t camera_device={.base={.name="ov5640",.state=DEVICE_STATE_UNINIT,.lifecycle=&sensor_lifecycle,.auto_init=true},.ops=&camera_ops};
DEVICE_EXPORT(camera_device);
SysResult_t Ov5640_ValidateConfig(const CameraConfig_t *c){return c && c->width==160 && c->height==120 &&
#ifdef STAGE03B_MJPEG_FIXTURE
 c->format==CAMERA_PIXEL_FORMAT_JPEG
#else
 c->format==CAMERA_PIXEL_FORMAT_YUV422
#endif
 ?SYS_OK:SYS_ERR_NOT_SUPPORTED;}
SysResult_t BSP_DCMI_SetEventCallback(BspDcmiEventCallback_t cb,void *p){camera_error_callback=cb;camera_error_context=p;return SYS_OK;}
SysResult_t BSP_DCMI_StartSnapshot(uint32_t *p,uint32_t n){(void)p;(void)n;return SYS_ERR_NOT_SUPPORTED;}
SysResult_t BSP_DCMI_StartContinuous(uint32_t *m0,uint32_t *m1,uint32_t n,BspDcmiFrameCallback_t cb,void *p){(void)n;dma_targets[0]=m0;dma_targets[1]=m1;camera_frame_callback=cb;camera_context=p;fake_dma_active=true;dcmi_starts++;return SYS_OK;}
SysResult_t BSP_DCMI_ReplaceTarget(BspDcmiTarget_t t,uint32_t *p){dma_targets[t]=p;return SYS_OK;}
SysResult_t BSP_DCMI_Stop(void){if(fake_dcmi_stop!=SYS_OK)return fake_dcmi_stop;fake_dma_active=false;return SYS_OK;}
bool BSP_DCMI_TakeEvents(BspDcmiEvents_t *e){if(fake_dcmi_events.flags==0)return false;*e=fake_dcmi_events;memset(&fake_dcmi_events,0,sizeof(fake_dcmi_events));return true;}
static void Select(RuntimeEventTarget_t target)
{
    TaskHandle_t handles[3]={s_camera_task,s_usb_task,s_control_task};uint32_t i;
    for(i=0;i<3;i++)if(i!=(uint32_t)target)vTaskSuspend(handles[i]);
    vTaskResume(handles[target]);vTaskSwitchContext();
}
static void Cam(void){Select(RUNTIME_TARGET_CAMERA);CameraTask_Process(RUNTIME_EVENT_BOOT_READY);}
static void Usb(void){Select(RUNTIME_TARGET_USB);UsbTask_Process(RUNTIME_EVENT_BOOT_READY);}
static void Control(void){Select(RUNTIME_TARGET_CONTROL);ControlTask_Process(RUNTIME_EVENT_BOOT_READY);}
static int Prepare(void)
{
    __HAL_RCC_USB_CONFIG(RCC_USBCLKSOURCE_HSI48);RCC->CR|=RCC_CR_HSI48RDY;
    EQ(App_TasksCreate(),SYS_OK);EQ(Framework_InitRuntime(),SYS_OK);EQ(start_count,0);EQ(dcmi_starts,0);EQ(sensor_starts,0);
    EQ(App_TasksRecordInitialization(SYS_OK),SYS_OK);EQ(Platform_RtosStart(),SYS_ERR_INVALID_STATE);
    NVIC->IP[OTG_FS_IRQn]=6U<<4;NVIC->IP[DMA1_Stream0_IRQn]=5U<<4;
    Select(RUNTIME_TARGET_CONTROL);EQ(App_TasksReleaseBoot(),SYS_OK);return 0;
}
static int Boot(void)
{
    EQ(Prepare(),0);Control();Cam();Control();Usb();Control();EQ(s_control_state,CONTROL_RUNNING);
    EQ(dcmi_starts,1);EQ(start_count,1);EQ(sensor_starts,1);return 0;
}
static int Host(void)
{
    Select(RUNTIME_TARGET_USB);HAL_PCD_ResetCallback(&s_pcd);Configured();EQ(Ep1_Commit(),0);EQ(Ep1_Open(),0);Usb();return 0;
}
static int CameraFrame(uint32_t target)
{
    uint32_t i;uint8_t *p=(uint8_t *)dma_targets[target];
    for(i=0;i<38400;i++)p[i]=(uint8_t)(i+target);
    test_ipsr=16U+DMA1_Stream0_IRQn;
    EQ(camera_frame_callback((BspDcmiTarget_t)target,camera_context),SYS_OK);
    test_ipsr=0;Cam();Usb();return 0;
}
static void Sof(void){Select(RUNTIME_TARGET_CAMERA);test_ipsr=16U+OTG_FS_IRQn;Tick();test_ipsr=0;Usb();}
static void CompleteUsb(void){Select(RUNTIME_TARGET_CAMERA);test_ipsr=16U+OTG_FS_IRQn;Done();test_ipsr=0;Usb();}
static int LoanFrame(void){EQ(Host(),0);EQ(CameraFrame(0),0);EQ(s_usb_bridge.state,BRIDGE_USB_LOANED);Sof();EQ(s_uvc_active->stream_state,USB_UVC_IN_FLIGHT);return 0;}
static void Pump(void){Usb();Cam();Control();}
static int Stop(void)
{
    uint32_t i;Select(RUNTIME_TARGET_CONTROL);EQ(ControlTask_RequestStop(),SYS_OK);Control();
    for(i=0;i<10;i++)Pump();
    EQ(s_control_state,CONTROL_IDLE);EQ(fake_dma_active,false);EQ(camera_device.base.state,DEVICE_STATE_READY);EQ(s_camera_bridge.held,false);EQ(s_usb_bridge.state,BRIDGE_USB_IDLE);return 0;
}
static int Stage02_Case(uint32_t id)
{
    uint32_t i,n,held;AppMessage_t old;CameraCaptureStats_t stats;
    if(id>=20 && id<=27){EQ(App_TasksCreate(),SYS_ERR_NO_RESOURCE);EQ(start_count,0);EQ(dcmi_starts,0);return 0;}
    if(id==28){__HAL_RCC_USB_CONFIG(RCC_USBCLKSOURCE_HSI48);RCC->CR|=RCC_CR_HSI48RDY;EQ(App_TasksCreate(),SYS_OK);fault=4;EQ(Framework_InitRuntime(),SYS_ERR_IO);EQ(App_TasksRecordInitialization(SYS_ERR_IO),SYS_ERR_IO);EQ(start_count,0);return 0;}
    if(id==0){EQ(Prepare(),0);Cam();Usb();EQ(start_count,0);EQ(dcmi_starts,0);return 0;}
    if(id==14){fake_camera_start=SYS_ERR_IO;EQ(Prepare(),0);Control();Cam();Control();Usb();Control();Cam();Control();EQ(dcmi_starts,0);EQ(g_control_task_debug.last_fault,SYS_ERR_IO);EQ(camera_device.base.state,DEVICE_STATE_ERROR);EQ(s_control_state,CONTROL_ERROR);return 0;}
    if(id==15){EQ(Prepare(),0);Control();Cam();Control();fault=7;Usb();fault=0;Control();for(i=0;i<8;i++)Pump();EQ(g_control_task_debug.last_fault,SYS_ERR_BUSY);EQ(fake_dma_active,false);return 0;}
    EQ(Boot(),0);
    if(id==1){for(i=0;i<20;i++){Sof();Cam();}EQ(g_bridge_debug.acquired_frames,0);return 0;}
    if(id==13){Select(RUNTIME_TARGET_CONTROL);EQ(ControlTask_RequestStop(),SYS_OK);Control();s_test_tick+=5001;Control();EQ(s_control_state,CONTROL_ERROR);EQ(fake_dma_active,true);return 0;}
    EQ(LoanFrame(),0);old=s_usb_bridge.lease;
    if(id==32){
        /* Simulated 33ms camera source: target FPS is not a measured throughput claim. */
        for(i=1;i<=1000;i++){
            s_test_tick=i;
            if(i%33U==0U)EQ(CameraFrame((i/33U)&1U),0);
            Sof();if(s_tx_transfer[1].active)CompleteUsb();Cam();Control();
        }
        EQ(g_usb_task_debug.completed_frames,25U);
        EQ(g_usb_task_debug.measured_fps_milli,g_usb_task_debug.completed_frames*1000U);
        EQ(g_camera_task_debug.dropped_frames>0,true);return 0;
    }
    if(id==30){for(i=0;i<75;i++)Tick();Usb();EQ(g_usb_task_debug.max_sof_gap>=75,true);EQ(g_usb_task_debug.coalesced_sof_slots>=74,true);return 0;}
    if(id==31){Select(RUNTIME_TARGET_CAMERA);EQ(App_EventsWait(RUNTIME_TARGET_CAMERA,0,&n),SYS_OK);EQ((n&RUNTIME_EVENT_CAMERA_PROGRESS)!=0,true);return 0;}
    if(id==3){held=s_capture.stream.in_use_index;for(i=0;i<30;i++)EQ(CameraFrame(i&1),0);EQ(s_capture.stream.in_use_index,held);EQ(s_capture.stream.buffers[held].state,CAMERA_BUFFER_IN_USE);EQ(CameraCapture_GetStats(&stats),SYS_OK);EQ(stats.dropped_frames==29,true);EQ(g_bridge_debug.acquired_frames,1);return 0;}
    if(id==4){EQ(Stop(),0);EQ(sensor_stops,1);return 0;}
    if(id==5){fault=20;Select(RUNTIME_TARGET_CONTROL);EQ(ControlTask_RequestStop(),SYS_OK);Control();for(i=0;i<5;i++)Pump();EQ(s_camera_bridge.held,true);EQ(sensor_stops,0);EQ(s_uvc_payload.frame!=NULL,true);fault=0;for(i=0;i<4;i++)Pump();EQ(Stop(),0);return 0;}
    if(id==6||id==7||id==8){Select(RUNTIME_TARGET_USB);if(id==6)HAL_PCD_ResetCallback(&s_pcd);else if(id==7)HAL_PCD_DisconnectCallback(&s_pcd);else Request(1,USB_REQ_SET_INTERFACE,0,1,0);for(i=0;i<5;i++)Pump();EQ(g_bridge_debug.released_frames,1);EQ(s_camera_bridge.held,false);return 0;}
    if(id==9){fake_dcmi_events.flags=BSP_DCMI_EVENT_ERROR;fake_dcmi_events.result=SYS_ERR_IO;test_ipsr=16U+DMA1_Stream0_IRQn;camera_error_callback(BSP_DCMI_NOTIFY_ERROR,camera_error_context);test_ipsr=0;Cam();EQ(fake_dma_active,false);EQ(s_capture.state,CAMERA_CAPTURE_DRAINING);EQ(s_camera_bridge.held,true);Control();for(i=0;i<10;i++)Pump();EQ(g_bridge_debug.released_frames,1);EQ(sensor_stops,1);EQ(g_control_task_debug.last_fault,SYS_ERR_IO);return 0;}
    if(id==10){USB_UVC_CancelFrame();Usb();Usb();held=s_capture.stream.in_use_index;s_capture.stream.in_use_index=UINT8_MAX;Cam();EQ(s_camera_bridge.held,true);EQ(g_bridge_debug.released_frames,0);s_capture.stream.in_use_index=held;Cam();Usb();EQ(g_bridge_debug.released_frames,1);return 0;}
    if(id==11){USB_UVC_CancelFrame();Usb();Usb();old.type=APP_MESSAGE_FRAME_RETURN;old.lease_id++;Select(RUNTIME_TARGET_USB);EQ(App_MessagesSend(APP_CHANNEL_TO_CAMERA,&old),SYS_OK);Cam();EQ(g_bridge_debug.released_frames,1);return 0;}
    if(id==12){EQ(Stop(),0);Select(RUNTIME_TARGET_CONTROL);EQ(ControlTask_RequestStart(),SYS_OK);for(i=0;i<8;i++){Control();Cam();Usb();}EQ(s_camera_bridge.epoch,2);EQ(Host(),0);EQ(CameraFrame(0),0);old.type=APP_MESSAGE_FRAME_RETURN;Select(RUNTIME_TARGET_USB);EQ(App_MessagesSend(APP_CHANNEL_TO_CAMERA,&old),SYS_OK);Cam();EQ(s_camera_bridge.held,true);EQ(g_bridge_debug.released_frames,1);return 0;}
    if(id==16){Select(RUNTIME_TARGET_CAMERA);EQ(App_MessagesSend(APP_CHANNEL_USB_COMMAND,&old),SYS_ERR_INVALID_STATE);return 0;}
    if(id==17){Select(RUNTIME_TARGET_USB);EQ(App_MessagesReceive(APP_CHANNEL_TO_CAMERA,&old),false);return 0;}
    if(id==18){Select(RUNTIME_TARGET_CAMERA);n=g_camera_task_debug.wake_count;test_ipsr=16U+OTG_FS_IRQn;CameraTask_Process(RUNTIME_EVENT_CAMERA_PROGRESS);test_ipsr=0;EQ(g_camera_task_debug.wake_count,n);return 0;}
    if(id==19){Select(RUNTIME_TARGET_USB);for(i=0;i<4;i++)EQ(App_MessagesSend(APP_CHANNEL_TO_CAMERA,&old),SYS_OK);EQ(App_MessagesSend(APP_CHANNEL_TO_CAMERA,&old),SYS_ERR_NO_RESOURCE);EQ(s_camera_bridge.held,true);return 0;}
    /* Drive actual Payload/PIO and task-side confirmation, without 20ms send polling. */
    n=0;
    while(g_bridge_debug.released_frames==0 && n++<100){
        if(s_tx_transfer[1].active)CompleteUsb();else Sof();
        Cam();
    }
    EQ(g_bridge_debug.released_frames,1);EQ(USB_UVC_GetCompletedFrameCount(),1);
    if(id==29){s_test_tick=1000;Usb();EQ(g_usb_task_debug.measured_fps_milli,1000);EQ(g_usb_task_debug.completed_frames,1);return 0;}
    EQ(g_bridge_debug.submitted_frames,1);return 0;
}

volatile uint32_t g_native_observation[13];
int Stage02_TestRun(uint32_t id)
{
    int result=Stage02_Case(id);
    g_native_observation[0]=s_control_state;
    g_native_observation[1]=g_camera_task_debug.state;
    g_native_observation[2]=g_usb_task_debug.state;
    g_native_observation[3]=g_bridge_debug.acquired_frames;
    g_native_observation[4]=g_bridge_debug.submitted_frames;
    g_native_observation[5]=g_bridge_debug.released_frames;
    g_native_observation[6]=g_camera_task_debug.captured_frames;
    g_native_observation[7]=g_camera_task_debug.dropped_frames;
    g_native_observation[8]=g_usb_task_debug.completed_frames;
    g_native_observation[9]=g_usb_task_debug.measured_fps_milli;
    g_native_observation[10]=g_camera_task_debug.measured_fps_milli;
    g_native_observation[11]=g_usb_task_debug.max_sof_gap;
    g_native_observation[12]=s_camera_bridge.held;
    return result;
}

//旧格式专项不产生JPEG事件，新增接口显式返回不支持
#ifndef STAGE03B_MJPEG_FIXTURE
SysResult_t BSP_DCMI_StartJpegSnapshot(uint32_t *p,uint32_t n){(void)p;(void)n;return SYS_ERR_NOT_SUPPORTED;}
SysResult_t BSP_DCMI_ProcessJpeg(uint32_t *n){(void)n;return SYS_ERR_NOT_SUPPORTED;}

#endif
