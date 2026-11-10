//编译基线与当前真实Task，记录外部调用、消息和每个现场的业务及诊断状态
#include "usb_task.h"
#include "app_events.h"
#include "app_status.h"
#include "usb_mode.h"
#include "usb_uvc.h"
#include "usb_device.h"
#ifndef B2_BASELINE
#include B2_APP_SOURCE
#endif
#include B2_TASK_SOURCE
#include <string.h>

//保存每次依赖调用的可比较记录，不接触真实USB硬件
volatile uint32_t g_trace[1024][30];
volatile uint32_t g_trace_count;
volatile uint32_t g_failure;
static uint32_t tick, sof, eof_count, waits, assertions, received, queued;
static uint32_t ack_failures, fault_failures, bridge_wait;
static bool owner=true, initialized=true, configured, bridge_idle=true, class_exists=true, frame_owned;
static bool observe_eof, auto_loan, fake_task_fail;
static UsbMode_t mode=USB_MODE_UVC;
static UsbModeState_t mode_state=USB_MODE_STATE_IDLE;
static UsbUvcStreamState_t stream_state=USB_UVC_WAIT_FRAME;
static SysResult_t start_result, stop_result, mode_result, abort_result, stream_result, query_result;
static AppMessage_t commands[8];
static uint32_t irq_number, scheduler_state=taskSCHEDULER_NOT_STARTED;

//在依赖入口读取Debug和唯一业务上下文，检测更新时点是否改变
static void Trace(uint32_t call,uint32_t a,uint32_t b)
{
    uint32_t n=g_trace_count,i;
    const volatile uint32_t *debug=(const volatile uint32_t*)&g_usb_task_debug;
    if(n>=1024){g_failure=9001;return;}
    g_trace[n][0]=call;g_trace[n][1]=a;g_trace[n][2]=b;g_trace[n][3]=tick;
    for(i=0;i<12;i++)g_trace[n][4+i]=debug[i];
#ifdef B2_BASELINE
    g_trace[n][16]=s_usb_started;g_trace[n][17]=s_usb_stopping;
    g_trace[n][18]=s_usb_stop_command;g_trace[n][19]=s_usb_ack_pending;
    g_trace[n][20]=s_usb_fault;g_trace[n][21]=s_usb_fault_reported;
    g_trace[n][22]=s_usb_fps_tick;g_trace[n][23]=s_usb_fps_frames;
#else
    g_trace[n][16]=s_usb.started;g_trace[n][17]=s_usb.stopping;
    g_trace[n][18]=s_usb.stop_command;g_trace[n][19]=s_usb.ack_pending;
    g_trace[n][20]=s_usb.fault;g_trace[n][21]=s_usb.fault_reported;
    g_trace[n][22]=s_usb.fps_tick;g_trace[n][23]=s_usb.fps_frames;
#endif
    g_trace[n][24]=mode_state;g_trace[n][25]=frame_owned;g_trace[n][26]=bridge_idle;
    g_trace[n][27]=stream_state;g_trace[n][28]=eof_count;g_trace[n][29]=sof;
    g_trace_count++;
}

//提供独立字节循环，避免编译器辅助函数递归调用自身
void *memset(void*d,int v,size_t n){volatile unsigned char*p=d;while(n--)*p++=(unsigned char)v;return d;}
void *memcpy(void*d,const void*s,size_t n){volatile unsigned char*p=d;const volatile unsigned char*q=s;while(n--)*p++=*q++;return d;}
int memcmp(const void*a,const void*b,size_t n){const unsigned char*p=a,*q=b;while(n--){if(*p!=*q)return *p-*q;p++;q++;}return 0;}
void __aeabi_memclr(void*d,size_t n){volatile unsigned char*p=d;while(n--)*p++=0;}
void __aeabi_memclr4(void*d,size_t n){volatile unsigned char*p=d;while(n--)*p++=0;}
void __aeabi_memclr8(void*d,size_t n){volatile unsigned char*p=d;while(n--)*p++=0;}
void __aeabi_memcpy(void*d,const void*s,size_t n){volatile unsigned char*p=d;const volatile unsigned char*q=s;while(n--)*p++=*q++;}
void __aeabi_memcpy4(void*d,const void*s,size_t n){volatile unsigned char*p=d;const volatile unsigned char*q=s;while(n--)*p++=*q++;}
void __aeabi_memcpy8(void*d,const void*s,size_t n){volatile unsigned char*p=d;const volatile unsigned char*q=s;while(n--)*p++=*q++;}

//仅模拟任务资源及阻塞出口，不启动真实调度器
uint32_t Platform_TimeGetMs(void){Trace(1,0,0);return tick++;}
uint32_t Platform_SyncGetException(void){return irq_number;}
bool App_MessagesIsOwner(RuntimeEventTarget_t t){return owner&&t==RUNTIME_TARGET_USB;}
BaseType_t xTaskGetSchedulerState(void){return scheduler_state;}
TaskHandle_t xTaskCreateStatic(TaskFunction_t f,const char*n,const uint32_t w,void*a,UBaseType_t p,StackType_t*s,StaticTask_t*t)
{(void)f;(void)n;(void)a;(void)s;(void)t;Trace(2,w,p);return fake_task_fail?NULL:(TaskHandle_t)0x2003E000;}
SysResult_t App_EventsAttach(RuntimeEventTarget_t t,TaskHandle_t h){Trace(3,t,h!=NULL);return SYS_OK;}
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t t){(void)t;Trace(4,0,0);return 1800;}
void Platform_RtosAssert(const char*f,uint32_t l){(void)f;(void)l;assertions++;Trace(5,assertions,0);}
SysResult_t App_EventsWait(RuntimeEventTarget_t t,uint32_t ms,uint32_t*events)
{
    (void)t;Trace(6,ms,0);waits++;
    if(waits==8){g_failure=9002;for(;;);}
    *events=waits==1?RUNTIME_EVENT_BOOT_READY:0;tick+=501;
    if(waits==2)return SYS_ERR_IO;
    return waits==3?SYS_ERR_TIMEOUT:SYS_OK;
}

//固定控制队列及独立回复故障注入，保留完整消息字节哈希
bool App_MessagesReceive(AppChannel_t c,AppMessage_t*m)
{Trace(7,c,queued);if(!queued)return false;*m=commands[received++];queued--;Trace(8,m->type,m->command_id);return true;}
SysResult_t App_MessagesSend(AppChannel_t c,const AppMessage_t*m)
{
    uint32_t h=0,i;const unsigned char*p=(const unsigned char*)m;
    for(i=0;i<sizeof(*m);i++)h=h*33+p[i];
    Trace(9,c,m->type);Trace(10,m->command_id,h);Trace(11,m->result,0);
    if(m->type==APP_MESSAGE_ACK&&ack_failures){ack_failures--;return SYS_ERR_NO_RESOURCE;}
    if(m->type==APP_MESSAGE_FAULT&&fault_failures){fault_failures--;return SYS_ERR_NO_RESOURCE;}
    return SYS_OK;
}
SysResult_t App_StatusPublishUsb(const AppUsbStatus_t*s)
{Trace(12,s->state,s->result);Trace(13,s->completed_frames,s->fps_milli);return SYS_OK;}

//Mode桩区分START接受、主机配置和底层错误，不在Task中伪造枚举
UsbMode_t USB_Mode_GetMode(void){Trace(14,mode,0);return mode;}
SysResult_t USB_Mode_Start(void)
{
    Trace(15,mode,0);
    if(!initialized)return SYS_ERR_INVALID_STATE;
    if(mode==USB_MODE_NONE)return SYS_OK;
    if(start_result!=SYS_OK){mode_state=USB_MODE_STATE_ERROR;mode_result=start_result;return start_result;}
    mode_state=USB_MODE_STATE_WAIT_CONFIGURED;return SYS_OK;
}
void USB_Mode_Process(void)
{
    Trace(16,mode_state,0);
    if(mode_state==USB_MODE_STATE_WAIT_CONFIGURED&&configured)mode_state=USB_MODE_STATE_RUNNING;
}
SysResult_t USB_Mode_GetLastResult(void){Trace(17,mode_result,0);return mode_result;}
SysResult_t USB_Mode_Stop(void)
{
    Trace(18,0,0);
    if(stop_result==SYS_OK)mode_state=USB_MODE_STATE_IDLE;
    else mode_state=USB_MODE_STATE_ERROR;
    return stop_result;
}

//UVC桩保持取消请求与实际归还分开，Abort失败继续持帧
uint32_t USB_Device_GetSofSequence(void){Trace(19,sof,0);return sof;}
uint32_t USB_UVC_GetCompletedFrameCount(void){Trace(20,eof_count,0);return eof_count;}
SysResult_t USB_UVC_CancelFrame(void)
{
    Trace(21,0,0);
    if(!class_exists)return SYS_ERR_INVALID_STATE;
    if(stream_state!=USB_UVC_WAIT_FRAME)stream_state=USB_UVC_CANCELLING;
    return SYS_OK;
}
void USB_UVC_Process(void)
{
    Trace(22,stream_state,0);
    if(!class_exists)return;
    if(stream_state==USB_UVC_CANCELLING||stream_state==USB_UVC_RECOVERY){
        if(abort_result==SYS_OK){frame_owned=false;stream_state=USB_UVC_WAIT_FRAME;}
        else {stream_state=USB_UVC_RECOVERY;stream_result=abort_result;}
    }else if(observe_eof&&frame_owned){frame_owned=false;eof_count++;stream_state=USB_UVC_WAIT_FRAME;observe_eof=false;}
}
void App_UvcBridgeUsbProcess(bool enabled)
{
    Trace(23,enabled,0);
    if(!enabled&&!frame_owned){if(bridge_wait)bridge_wait--;else bridge_idle=true;}
    if(enabled&&auto_loan&&!frame_owned&&configured){bridge_idle=false;frame_owned=true;stream_state=USB_UVC_IN_FLIGHT;}
}
bool App_UvcBridgeUsbIsIdle(void){Trace(24,bridge_idle,0);return bridge_idle;}
SysResult_t USB_UVC_GetStreamStatus(UsbUvcStreamStatus_t*s)
{
    Trace(25,0,0);
    if(query_result!=SYS_OK)return query_result;
    if(!class_exists)return SYS_ERR_INVALID_STATE;
    memset(s,0,sizeof(*s));s->state=stream_state;s->result=stream_result;s->frame_owned=frame_owned;
    s->can_submit=configured&&!frame_owned;return SYS_OK;
}

//把合法命令放入测试输入，不取代真实Task的出队门控
static void Push(AppMessageType_t type,uint32_t id)
{AppMessage_t*m=&commands[received+queued++];memset(m,0,sizeof(*m));m->type=type;m->source=RUNTIME_TARGET_CONTROL;m->command_id=id;}
static void Round(uint32_t events)
{UsbTask_Process(events);UsbTask_PublishStatus();Trace(26,0,0);}
static void Loan(void){frame_owned=true;bridge_idle=false;stream_state=USB_UVC_IN_FLIGHT;}

//每种冷启动模式执行同一组固定事件序列，追踪所有诊断和依赖调用
int B2_TestRun(uint32_t id)
{
    uint32_t i, scenario=id%32;
    mode=id/32==0?USB_MODE_UVC:(id/32==1?USB_MODE_TEST:USB_MODE_NONE);
#ifndef B2_BASELINE
    App_USBSetDiagnosticObserver(UsbTask_Observe);
#endif
    if(scenario==0){Round(0);Round(RUNTIME_EVENT_BOOT_READY);return 0;}
    if(scenario==1){owner=false;Round(RUNTIME_EVENT_BOOT_READY);return 0;}
    if(scenario==2){Push(APP_MESSAGE_STOP,1);Round(RUNTIME_EVENT_BOOT_READY);Round(0);return 0;}
    if(scenario==3){Push((AppMessageType_t)99,1);Round(RUNTIME_EVENT_BOOT_READY);return 0;}
    if(scenario==4)start_result=SYS_ERR_IO;
    if(scenario==5)initialized=false;
    if(scenario==6){start_result=SYS_ERR_IO;ack_failures=4;}
    Push(APP_MESSAGE_START,10);Round(RUNTIME_EVENT_BOOT_READY);
    if(scenario>=4&&scenario<=6){for(i=0;i<3;i++)Round(0);return 0;}
    if(scenario==7){configured=true;Round(0);return 0;}
    if(scenario==8){Push(APP_MESSAGE_START,11);Round(0);return 0;}
    if(scenario==9){mode_result=SYS_ERR_TIMEOUT;Round(0);for(i=0;i<2;i++)Round(0);return 0;}
    if(scenario==10){Loan();abort_result=SYS_ERR_IO;}
    if(scenario==11){bridge_idle=false;bridge_wait=2;}
    if(scenario==12){stop_result=SYS_ERR_IO;}
    if(scenario==13){stop_result=SYS_ERR_BUSY;}
    if(scenario==14){Loan();query_result=SYS_ERR_IO;abort_result=SYS_ERR_IO;}
    if(scenario==15){Loan();class_exists=false;}
    if(scenario==16){tick=2000;eof_count=50;Round(0);return 0;}
    if(scenario==17){tick=0xfffffff0;eof_count=0xfffffff0;Round(0);tick=1100;eof_count=5;Round(0);return 0;}
    if(scenario==18){tick=2000;eof_count=5000;Round(0);return 0;}
    if(scenario==19){g_usb_task_debug.result=SYS_ERR_IO;Round(0);return 0;}
    if(scenario==20){sof=10;Round(0);sof=11;Round(0);sof=15;Round(0);return 0;}
    if(scenario==21){g_usb_task_debug.sof_sequence=0xfffffff0;sof=4;Round(0);return 0;}
    if(scenario==22){ack_failures=5;fault_failures=3;Push(APP_MESSAGE_START,11);for(i=0;i<4;i++)Round(0);return 0;}
    if(scenario==23){Push(APP_MESSAGE_STOP,11);Push(APP_MESSAGE_START,12);for(i=0;i<4;i++)Round(0);return 0;}
    if(scenario==24){Push(APP_MESSAGE_STOP,11);Push(APP_MESSAGE_STOP,12);for(i=0;i<4;i++)Round(0);return 0;}
    if(scenario==25){Loan();observe_eof=true;Round(0);}
    if(scenario==26){auto_loan=true;configured=true;Round(0);}
    if(scenario==27){UsbTask_Run(NULL);return 0;}
    if(scenario==28){return UsbTask_Create();}
    if(scenario==29){UsbTask_Create();return UsbTask_Create()==SYS_ERR_ALREADY_EXISTS?0:__LINE__;}
    if(scenario==30){fake_task_fail=true;return UsbTask_Create()==SYS_ERR_NO_RESOURCE?0:__LINE__;}
    if(scenario==31){Push(APP_MESSAGE_START,11);Round(0);Push(APP_MESSAGE_STOP,12);Round(0);Push(APP_MESSAGE_START,13);Round(0);return 0;}
    Push(APP_MESSAGE_STOP,11);Round(0);
    //失败或BUSY仍继续后台停止，恢复桩条件后再推进。
    abort_result=SYS_OK;stop_result=SYS_OK;query_result=SYS_OK;class_exists=true;
    frame_owned=false;bridge_idle=true;
    for(i=0;i<3;i++)Round(0);
    return 0;
}

//独立验证只读Getter、ACK/FAULT保留和两个确认入口互不干扰
int B2_InterfaceRun(uint32_t id)
{
#ifndef B2_BASELINE
    AppUsbRuntimeStatus_t before,after;
    AppMessage_t command={.type=APP_MESSAGE_START},message;
    uint32_t count;
    App_USBSetDiagnosticObserver(UsbTask_Observe);
    if(id==0){App_USBHandleCommand(NULL);return App_USBCanAcceptCommand()?0:__LINE__;}
    if(id==1)return !App_USBGetPendingAck(NULL)&&!App_USBGetPendingFault(NULL)?0:__LINE__;
    command.type=APP_MESSAGE_START;command.command_id=9;command.source=RUNTIME_TARGET_CONTROL;
    App_USBHandleCommand(&command);
    count=g_trace_count;memset(&before,0,sizeof(before));memset(&after,0,sizeof(after));
    App_USBGetStatus(&before);App_USBGetStatus(&after);App_USBGetLastResult();
    App_USBCanTransfer();App_USBIsStopping();App_USBCanAcceptCommand();
    if(g_trace_count!=count||memcmp(&before,&after,sizeof(before)))return __LINE__;
    if(id==2){if(!App_USBGetPendingAck(&message)||App_USBCanAcceptCommand())return __LINE__;if(!App_USBGetPendingAck(&message))return __LINE__;App_USBConfirmAckEnqueued();if(!App_USBCanAcceptCommand())return __LINE__;}
    if(id==3){s_usb.fault=SYS_ERR_IO;if(!App_USBGetPendingFault(&message))return __LINE__;App_USBConfirmFaultEnqueued();if(!s_usb.ack_pending||App_USBGetPendingFault(&message))return __LINE__;}
    if(id==4){s_usb.fault=SYS_ERR_IO;App_USBConfirmAckEnqueued();if(!App_USBGetPendingFault(&message))return __LINE__;}
    if(id==5){App_USBSetDiagnosticObserver(NULL);command.command_id=12;App_USBConfirmAckEnqueued();App_USBHandleCommand(&command);App_USBGetStatus(&after);if(after.command_id!=12||after.last_result!=SYS_ERR_BUSY)return __LINE__;}
#endif
    return 0;
}
