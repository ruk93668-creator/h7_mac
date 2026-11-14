//编译真实App_Camera和CameraTask，依赖用可记录顺序的故障桩替换
#include "camera_task.h"
#include "app_camera.h"
#include "app_events.h"
#include "app_status.h"
#include "camera_capture.h"
#include "ov5640_device.h"
#include "video_config.h"
#include <string.h>
#include B1_APP_SOURCE
#include B1_TASK_SOURCE

//保存逐调用轨迹及当时的Debug和业务标记
volatile uint32_t g_trace[1024][25];
volatile uint32_t g_trace_count;
volatile uint32_t g_failure;
static uint32_t tick, case_id, calls, received, queue_count, assert_count;
static bool owner = true, ready, stopped, bridge_idle = true, stats_valid = true;
static SysResult_t bridge_result, init_result, start_result, stop_result, process_result;
static SysResult_t device_start_result, device_stop_result, configure_result;
static uint32_t send_failures, stop_busy_rounds, stop_error_rounds, bridge_busy_rounds;
static AppMessage_t queue[8];
static CameraCaptureStats_t media_stats;
static VideoConfig_t config = {{160,120,CAMERA_PIXEL_FORMAT_YUV422}, VIDEO_OUTPUT_USB_UVC,25,0};
static Ov5640Device_t sensor;

//记录依赖调用现场，比较状态更新相对于外部调用的先后
static void Trace(uint32_t code, uint32_t a, uint32_t b)
{
    uint32_t n=g_trace_count, i;
    const volatile uint32_t *debug=(const volatile uint32_t *)&g_camera_task_debug;
    if(n>=1024){g_failure=9001;return;}
    g_trace[n][0]=code;g_trace[n][1]=a;g_trace[n][2]=b;g_trace[n][3]=tick;
    for(i=0;i<11;i++)g_trace[n][4+i]=debug[i];
#ifdef B1_BASELINE
    g_trace[n][15]=s_camera_running;g_trace[n][16]=s_camera_prepared;
    g_trace[n][17]=s_camera_stopping;g_trace[n][18]=s_camera_ack_pending;
    g_trace[n][19]=s_camera_stop_command;g_trace[n][20]=s_camera_fault;
    g_trace[n][21]=s_camera_fault_sent;g_trace[n][22]=s_camera_fps_tick;
    g_trace[n][23]=s_camera_fps_frames;
#else
    g_trace[n][15]=s_camera.running;g_trace[n][16]=s_camera.prepared;
    g_trace[n][17]=s_camera.stopping;g_trace[n][18]=s_camera.ack_pending;
    g_trace[n][19]=s_camera.stop_command;g_trace[n][20]=s_camera.fault;
    g_trace[n][21]=s_camera.fault_sent;g_trace[n][22]=s_camera.fps_tick;
    g_trace[n][23]=s_camera.fps_frames;
#endif
    g_trace[n][24]=sensor.base.state;g_trace_count++;
}

//提供编译器结构复制所需的有界测试实现
void *memset(void *d,int v,size_t n){volatile unsigned char *p=d;while(n--)*p++=(unsigned char)v;return d;}
void *memcpy(void *d,const void *s,size_t n){volatile unsigned char *p=d;const volatile unsigned char *q=s;while(n--)*p++=*q++;return d;}
void __aeabi_memclr4(void*d,size_t n){volatile unsigned char*p=d;while(n--)*p++=0;}
void __aeabi_memclr8(void*d,size_t n){volatile unsigned char*p=d;while(n--)*p++=0;}
void __aeabi_memclr(void*d,size_t n){volatile unsigned char*p=d;while(n--)*p++=0;}
void __aeabi_memcpy4(void*d,const void*s,size_t n){volatile unsigned char*p=d;const volatile unsigned char*q=s;while(n--)*p++=*q++;}
void __aeabi_memcpy8(void*d,const void*s,size_t n){volatile unsigned char*p=d;const volatile unsigned char*q=s;while(n--)*p++=*q++;}
void __aeabi_memcpy(void*d,const void*s,size_t n){volatile unsigned char*p=d;const volatile unsigned char*q=s;while(n--)*p++=*q++;}
uint32_t __aeabi_uidiv(uint32_t a,uint32_t b){uint32_t q=0,i,rem=0,hi;if(!b)return 0;for(i=32;i>0;){i--;hi=rem>>31;rem=(rem<<1)|((a>>i)&1U);if(hi || rem>=b){rem-=b;q|=1U<<i;}}return q;}

//保留实际时间读取次数，用于验证启动和FPS取时位置
uint32_t Platform_TimeGetMs(void){Trace(1,0,0);return tick++;}
uint32_t Platform_SyncGetException(void){return 0;}
bool App_MessagesIsOwner(RuntimeEventTarget_t t){(void)t;return owner
#ifdef B1_INTEGRATION
 && t == integration_owner
#endif
 ;}
SysResult_t App_EventsAttach(RuntimeEventTarget_t t,TaskHandle_t h){Trace(2,t,h!=NULL);return SYS_OK;}
BaseType_t xTaskGetSchedulerState(void){return taskSCHEDULER_NOT_STARTED;}
TaskHandle_t xTaskCreateStatic(TaskFunction_t f,const char *n,const uint32_t words,void *a,UBaseType_t pri,StackType_t *s,StaticTask_t *t)
{(void)n;(void)a;(void)s;(void)t;Trace(3,words,pri);(void)f;return (TaskHandle_t)0x2003E000;}
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t t){(void)t;Trace(4,0,0);return 1200;}
void Platform_RtosAssert(const char *file,uint32_t line){(void)file;(void)line;assert_count++;Trace(5,assert_count,0);}
SysResult_t App_EventsWait(RuntimeEventTarget_t target,uint32_t ms,uint32_t *events)
{
    (void)target;Trace(6,ms,0);calls++;
    //最后一次等待由运行器停止模拟，避免实际无限任务循环
    if(calls==8){g_failure=9002;for(;;);}
    *events=calls==1?RUNTIME_EVENT_BOOT_READY:0;
    if(calls==2)return SYS_ERR_IO;
    tick+=501;
    return calls==3?SYS_ERR_TIMEOUT:SYS_OK;
}

//模拟FIFO消息出队与独立ACK、FAULT发送失败
#ifndef B1_INTEGRATION
bool App_MessagesReceive(AppChannel_t c,AppMessage_t *m)
{
    Trace(7,c,queue_count);
    if(!queue_count)return false;
    *m=queue[received++];queue_count--;Trace(8,m->type,m->command_id);return true;
}
SysResult_t App_MessagesSend(AppChannel_t c,const AppMessage_t *m)
{
    uint32_t hash=0,i;const unsigned char *p=(const unsigned char*)m;
    for(i=0;i<sizeof(*m);i++)hash=hash*33+p[i];
    Trace(9,m->type,m->result);Trace(10,m->command_id,hash);
    (void)c;
    if(send_failures){send_failures--;return SYS_ERR_NO_RESOURCE;}
    return SYS_OK;
}
SysResult_t App_StatusPublishCamera(const AppCameraStatus_t *s)
{
    Trace(11,s->state,s->result);Trace(12,s->captured_frames,s->fps_milli);
    Trace(13,s->config_valid,((uint32_t)s->format<<24)|(s->width<<12)|s->height);
    return SYS_OK;
}

#endif

//模拟Bridge公开入口，保持Task与模块边界不变
#ifndef B1_INTEGRATION
SysResult_t App_UvcBridgeCameraBegin(void){Trace(14,0,0);return bridge_result;}
void App_UvcBridgeCameraProcess(bool enabled){Trace(15,enabled,0);if(bridge_busy_rounds && --bridge_busy_rounds==0)bridge_idle=true;}
bool App_UvcBridgeCameraIsIdle(void){Trace(16,bridge_idle,0);return bridge_idle;}

#endif

#ifndef B1_REAL_MEDIA
//提供新增只读事实依赖，不推进桩状态或增加原轨迹
SysResult_t CameraCapture_GetStopFacts(CameraCaptureStopFacts_t *facts)
{
    if (facts == NULL) return SYS_ERR_INVALID_ARG;
    facts->attempt_sequence = 0U;
    facts->hardware_result = SYS_OK;
    facts->flags = (ready ? CAMERA_STOP_INITIALIZED : 0U) | (stopped ? CAMERA_STOP_COMPLETE : 0U);
    return SYS_OK;
}
#endif

//模拟Device和Media的正常、部分准备及停止失败路径
const VideoConfig_t *VideoConfig_GetDefault(void){Trace(17,0,0);return &config;}
SysResult_t Ov5640_ValidateConfig(const CameraConfig_t *c){Trace(18,c->width,c->height);return configure_result;}
#ifndef B1_REAL_MEDIA
SysResult_t CameraCapture_ValidateStreamConfig(const CameraConfig_t *c){(void)c;Trace(19,0,0);return SYS_OK;}
#endif
static SysResult_t Configure(Ov5640Device_t*d,const CameraConfig_t*c){(void)d;(void)c;Trace(20,0,0);return configure_result;}
static const Ov5640Ops_t ops={NULL,Configure};
Device_t *Device_Find(const char *n){(void)n;Trace(21,0,0);sensor.ops=&ops;return &sensor.base;}
SysResult_t Device_Start(Device_t*d){Trace(22,0,0);if(device_start_result==SYS_OK)d->state=DEVICE_STATE_RUNNING;return device_start_result;}
SysResult_t Device_Stop(Device_t*d){Trace(23,0,0);if(device_stop_result==SYS_OK)d->state=DEVICE_STATE_READY;else d->state=DEVICE_STATE_ERROR;return device_stop_result;}
#ifndef B1_REAL_MEDIA
SysResult_t CameraCapture_Init(const CameraConfig_t*c){(void)c;Trace(24,0,0);if(init_result==SYS_OK)ready=true;return init_result;}
#endif
#ifndef B1_REAL_MEDIA
SysResult_t CameraCapture_StartStream(void){Trace(25,0,0);stopped=false;return start_result;}
#endif
#ifndef B1_REAL_MEDIA
SysResult_t CameraCapture_Start(void){Trace(26,0,0);return start_result;}
#endif
#ifndef B1_REAL_MEDIA
SysResult_t CameraCapture_Stop(void)
{
    Trace(27,0,0);
#ifdef B1_INTEGRATION
    if(integration_usb_ref || integration_sd_ref)return SYS_ERR_BUSY;
#endif
    if(stop_busy_rounds){stop_busy_rounds--;return SYS_ERR_BUSY;}
    if(stop_error_rounds){stop_error_rounds--;return SYS_ERR_IO;}
    if(stop_result==SYS_OK && case_id!=14)stopped=true;
    return stop_result;
}
#endif
#ifndef B1_REAL_MEDIA
bool CameraCapture_IsStopped(void){Trace(28,stopped,0);return stopped;}
#endif
#ifndef B1_REAL_MEDIA
SysResult_t CameraCapture_GetLastResult(void){Trace(29,0,0);return ready?process_result:SYS_ERR_INVALID_STATE;}
#endif
#ifndef B1_REAL_MEDIA
void CameraCapture_Process(void){Trace(30,0,0);}
#endif
#ifndef B1_REAL_MEDIA
SysResult_t CameraCapture_GetStats(CameraCaptureStats_t*s){Trace(31,0,0);if(!stats_valid)return SYS_ERR_IO;*s=media_stats;return SYS_OK;}
#endif

//将命令加入固定测试队列，不替代生产消息处理逻辑
static void Push(AppMessageType_t type,uint32_t id)
{
    AppMessage_t *m=&queue[received+queue_count++];
    memset(m,0,sizeof(*m));m->type=type;m->source=RUNTIME_TARGET_CONTROL;m->command_id=id;
}

//执行一轮真实Task处理与状态发布，保持两个调用位置
static void Round(uint32_t events)
{
    CameraTask_Process(events);
    CameraTask_PublishStatus();
    Trace(32,0,0);
}

//执行固定异常序列，每个用例从独立的零初始化内存开始
int B1_TestRun(uint32_t id)
{
    uint32_t i;
    case_id=id;sensor.base.state=DEVICE_STATE_READY;
#ifndef B1_BASELINE
    App_CameraSetDiagnosticObserver(CameraTask_Observe);
#endif
    if(id==0){Round(0);Round(RUNTIME_EVENT_BOOT_READY);return 0;}
    if(id==1){owner=false;Round(RUNTIME_EVENT_BOOT_READY);return 0;}
    if(id==2){Push(APP_MESSAGE_STOP,1);Round(RUNTIME_EVENT_BOOT_READY);Round(0);return 0;}
    if(id==3){Push((AppMessageType_t)99,1);Round(RUNTIME_EVENT_BOOT_READY);return 0;}
    if(id==4)bridge_result=SYS_ERR_BUSY;
    if(id==5)bridge_result=SYS_ERR_IO;
    if(id==6)configure_result=SYS_ERR_IO;
    if(id==7)init_result=SYS_ERR_IO;
    if(id==8)device_start_result=SYS_ERR_IO;
    if(id==9)start_result=SYS_ERR_IO;
    if(id==10){start_result=SYS_ERR_IO;stop_error_rounds=1;}
    Push(APP_MESSAGE_START,10);
    if(id==11)send_failures=5;
    Round(RUNTIME_EVENT_BOOT_READY);
    if(id>=4 && id<=11){for(i=0;i<4;i++)Round(0);return 0;}
    if(id==12){Push(APP_MESSAGE_START,11);Round(0);}
    if(id==13){process_result=SYS_ERR_IO;send_failures=3;Round(0);for(i=0;i<4;i++)Round(0);return 0;}
    if(id==14)stopped=false;
    if(id==15)stop_busy_rounds=3;
    if(id==16)stop_error_rounds=2;
    if(id==17){bridge_idle=false;bridge_busy_rounds=3;}
    if(id==18)device_stop_result=SYS_ERR_IO;
    if(id==19)stats_valid=false;
    if(id==20){tick=0xfffffff0;media_stats.completed_frames=0xfffffff0;Round(0);tick=1020;media_stats.completed_frames=4;Round(0);return 0;}
    if(id==21){tick=2000;media_stats.completed_frames=5000;Round(0);return 0;}
    if(id==22){g_camera_task_debug.result=SYS_ERR_IO;Round(0);return 0;}
    if(id==23){Push(APP_MESSAGE_STOP,11);Push(APP_MESSAGE_START,12);for(i=0;i<4;i++)Round(0);return 0;}
    if(id==24){Push(APP_MESSAGE_STOP,11);Push(APP_MESSAGE_STOP,12);for(i=0;i<4;i++)Round(0);return 0;}
    if(id==25){media_stats.completed_frames=30;tick=1001;Round(0);tick=2002;media_stats.completed_frames=60;Round(0);return 0;}
    if(id==26){ready=false;Push(APP_MESSAGE_STOP,11);Round(0);return 0;}
    if(id==27){send_failures=8;process_result=SYS_ERR_IO;Round(0);Push(APP_MESSAGE_START,11);for(i=0;i<6;i++)Round(0);return 0;}
    if(id==28){CameraTask_Run(NULL);return 0;}
    if(id==29){return CameraTask_Create();}
    if(id==30){CameraTask_Create();return CameraTask_Create()==SYS_ERR_ALREADY_EXISTS?0:__LINE__;}
    if(id==31){process_result=SYS_ERR_IO;Round(0);process_result=SYS_OK;Push(APP_MESSAGE_START,11);Round(0);process_result=SYS_ERR_TIMEOUT;Round(0);return 0;}
    if(id==32){config.output=VIDEO_OUTPUT_USB_TEST;Push(APP_MESSAGE_STOP,11);Round(0);return 0;}
    if(id==33){config.output=VIDEO_OUTPUT_NONE;Push(APP_MESSAGE_STOP,11);Round(0);return 0;}
    Push(APP_MESSAGE_STOP,11);
    for(i=0;i<5;i++){tick+=250;media_stats.completed_frames++;Round(0);}
    return 0;
}

//只读接口专项验证，不用快照反向修改生产业务
int B1_InterfaceRun(uint32_t id)
{
#ifndef B1_BASELINE
    AppCameraRuntimeStatus_t before, after;
    AppMessage_t m;
    uint32_t count;
    sensor.base.state=DEVICE_STATE_READY;
    App_CameraSetDiagnosticObserver(CameraTask_Observe);
    if(id==0){App_CameraHandleCommand(NULL);return App_CameraCanAcceptCommand()?0:__LINE__;}
    if(id==1){return !App_CameraGetPendingAck(NULL)&&!App_CameraGetPendingFault(NULL)?0:__LINE__;}
    Push(APP_MESSAGE_START,1);
    if(id==2)send_failures=100;
    Round(RUNTIME_EVENT_BOOT_READY);
    count=g_trace_count;memset(&before,0,sizeof(before));memset(&after,0,sizeof(after));App_CameraGetStatus(&before);
    App_CameraGetStatus(&after);App_CameraGetLastResult();
    if(g_trace_count!=count || memcmp(&before,&after,sizeof(before))!=0)return __LINE__;
    if(id==2){if(App_CameraCanAcceptCommand())return __LINE__;App_CameraGetPendingAck(&m);if(App_CameraCanAcceptCommand())return __LINE__;App_CameraConfirmAckEnqueued();if(!App_CameraCanAcceptCommand())return __LINE__;}
    if(id==3){s_camera.applied_valid=false;App_CameraGetStatus(&after);if(after.applied_valid)return __LINE__;App_CameraSampleStatus();App_CameraGetStatus(&after);if(!after.applied_valid)return __LINE__;}
    if(id==4){s_camera.fault=SYS_ERR_IO;s_camera.fault_sent=false;s_camera.ack_pending=true;if(!App_CameraGetPendingFault(&m))return __LINE__;App_CameraConfirmFaultEnqueued();if(!s_camera.ack_pending || App_CameraGetPendingFault(&m))return __LINE__;}
#endif
    return 0;
}
int memcmp(const void*a,const void*b,size_t n){const unsigned char*p=a,*q=b;while(n--){if(*p!=*q)return *p-*q;p++;q++;}return 0;}
