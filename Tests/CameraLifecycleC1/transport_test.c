//执行真实消息适配及FreeRTOS队列，任务身份和调度出口由测试控制
#include "app_messages.h"
#include "app_events.h"
#include "queue.h"
#include <string.h>

//包装真实静态创建函数，只对指定次创建注入失败
#define xQueueGenericCreateStatic C1_RealCreateStatic
#include "../../Middlewares/Third_Party/FreeRTOS/Source/queue.c"
#undef xQueueGenericCreateStatic
#include "../../Middlewares/Third_Party/FreeRTOS/Source/list.c"
static uint32_t creates,fail_create;
QueueHandle_t xQueueGenericCreateStatic(const UBaseType_t length,const UBaseType_t size,uint8_t*storage,StaticQueue_t*control,const uint8_t type)
{creates++;if(creates==fail_create)return NULL;return C1_RealCreateStatic(length,size,storage,control,type);}
#include C1_MESSAGES_SOURCE

volatile uint32_t g_failure;
volatile uint32_t g_trace[1024][8],g_trace_count;
static uint32_t exception,primask,basepri,critical,posts,post_targets;
static RuntimeEventTarget_t owner=RUNTIME_TARGET_CONTROL;
static BaseType_t scheduler=taskSCHEDULER_NOT_STARTED;
static bool notify_fails;
#define CHECK(x) do{if(!(x)){g_failure=__LINE__;return __LINE__;}}while(0)

//记录旧通道外部行为，比较基线与候选是否完全一致
static void Trace(uint32_t call,uint32_t a,uint32_t b,uint32_t c)
{uint32_t n=g_trace_count;if(n>=1024){g_failure=9001;return;}g_trace[n][0]=call;g_trace[n][1]=a;g_trace[n][2]=b;g_trace[n][3]=c;g_trace[n][4]=posts;g_trace[n][5]=post_targets;g_trace[n][6]=owner;g_trace[n][7]=creates;g_trace_count++;}
static uint32_t Hash(const void*p,uint32_t n){const uint8_t*q=p;uint32_t h=0;while(n--)h=h*33+*q++;return h;}
uint32_t Platform_SyncGetException(void){return exception;}
uint32_t Platform_SyncGetPrimask(void){return primask;}
uint32_t Platform_SyncGetBasepri(void){return basepri;}
bool App_EventsIsOwner(RuntimeEventTarget_t t){return owner==t;}
BaseType_t xTaskGetSchedulerState(void){return scheduler;}
SysResult_t RuntimeEvent_Post(RuntimeEventTarget_t t,uint32_t e){(void)e;posts++;post_targets|=1U<<t;return notify_fails?SYS_ERR_IO:SYS_OK;}
void Platform_RtosAssert(const char*f,uint32_t l){(void)f;g_failure=l;}

//队列使用真实复制和列表；本测试只提交零等待操作，不运行调度器
void vPortEnterCritical(void){critical++;}
void vPortExitCritical(void){if(critical)critical--;else g_failure=9002;}
void vTaskSuspendAll(void){}
BaseType_t xTaskResumeAll(void){return pdTRUE;}
BaseType_t xTaskRemoveFromEventList(const List_t*l){(void)l;g_failure=9003;return pdFALSE;}
void vTaskPlaceOnEventList(List_t*l,const TickType_t t){(void)l;(void)t;g_failure=9004;}
void vTaskInternalSetTimeOutState(TimeOut_t*t){t->xOverflowCount=0;t->xTimeOnEntering=0;}
BaseType_t xTaskCheckForTimeOut(TimeOut_t*t,TickType_t*w){(void)t;(void)w;return pdTRUE;}
void vTaskMissedYield(void){}
UBaseType_t uxTaskGetNumberOfTasks(void){return 0;}
void vPortValidateInterruptPriority(void){}

//构造旧消息的全部字节，覆盖最大身份字段和描述内容
static AppMessage_t Legacy(uint32_t id)
{AppMessage_t m;memset(&m,0,sizeof(m));m.type=APP_MESSAGE_START;m.source=owner;m.command_id=id;m.stream_epoch=0xFFFFFFFF;m.lease_id=0x12345678;m.result=SYS_ERR_IO;m.frame_id=99;m.timestamp_ms=234;m.frame.width=320;m.frame.height=240;m.frame.size_bytes=70000;return m;}

//旧通道保留52B、五条四项队列、原发送和接收权限
uint32_t C1_LegacyRun(uint32_t id)
{
 AppMessage_t m,r;uint32_t channel=id%5,who=id/5,i;SysResult_t result;bool allowed;
 CHECK(sizeof(AppMessage_t)==52);CHECK(App_MessagesInit()==SYS_OK);CHECK(creates==5);scheduler=taskSCHEDULER_RUNNING;
 if(id<25)
 {
  owner=(RuntimeEventTarget_t)who;m=Legacy(17);
  allowed=channel<2?owner==RUNTIME_TARGET_CONTROL:channel==3?owner==RUNTIME_TARGET_USB:channel==4?owner==RUNTIME_TARGET_CAMERA:true;
  result=App_MessagesSend((AppChannel_t)channel,&m);Trace(1,channel,result,Hash(&m,sizeof(m)));
  CHECK(result==(allowed?SYS_OK:SYS_ERR_INVALID_STATE));
  owner=s_message_receivers[channel];if(allowed){CHECK(App_MessagesReceive((AppChannel_t)channel,&r));CHECK(memcmp(&m,&r,sizeof(m))==0);Trace(2,channel,Hash(&r,sizeof(r)),0);}return 0;
 }
 if(id<50)
 {
  channel=(id-25)%5;owner=channel<2?RUNTIME_TARGET_CONTROL:channel==3?RUNTIME_TARGET_USB:RUNTIME_TARGET_CAMERA;m=Legacy(18);
  CHECK(App_MessagesSend((AppChannel_t)channel,&m)==SYS_OK);owner=(RuntimeEventTarget_t)((id-25)/5);
  allowed=owner==s_message_receivers[channel];CHECK(App_MessagesReceive((AppChannel_t)channel,&r)==allowed);Trace(3,channel,allowed,allowed?Hash(&r,sizeof(r)):0);return 0;
 }
 owner=RUNTIME_TARGET_CONTROL;m=Legacy(19);
 switch(id)
 {
 case 50:for(i=0;i<4;i++){m.command_id=i;CHECK(App_MessagesSend(APP_CHANNEL_CAMERA_COMMAND,&m)==0);}CHECK(App_MessagesSend(APP_CHANNEL_CAMERA_COMMAND,&m)==SYS_ERR_NO_RESOURCE);owner=RUNTIME_TARGET_CAMERA;for(i=0;i<4;i++){CHECK(App_MessagesReceive(APP_CHANNEL_CAMERA_COMMAND,&r));CHECK(r.command_id==i);}CHECK(!App_MessagesReceive(APP_CHANNEL_CAMERA_COMMAND,&r));Trace(4,posts,post_targets,0);break;
 case 51:CHECK(App_MessagesSend(APP_CHANNEL_COUNT,&m)==SYS_ERR_INVALID_ARG);CHECK(!App_MessagesReceive(APP_CHANNEL_COUNT,&r));Trace(5,0,0,0);break;
 case 52:CHECK(App_MessagesSend(APP_CHANNEL_CAMERA_COMMAND,NULL)==SYS_ERR_INVALID_ARG);CHECK(!App_MessagesReceive(APP_CHANNEL_CAMERA_COMMAND,NULL));Trace(6,0,0,0);break;
 case 53:exception=1;CHECK(App_MessagesSend(APP_CHANNEL_CAMERA_COMMAND,&m)==SYS_ERR_INVALID_STATE);Trace(7,0,0,0);break;
 case 54:notify_fails=true;CHECK(App_MessagesSend(APP_CHANNEL_CAMERA_COMMAND,&m)==SYS_OK);owner=RUNTIME_TARGET_CAMERA;CHECK(App_MessagesReceive(APP_CHANNEL_CAMERA_COMMAND,&r));Trace(8,Hash(&r,sizeof(r)),0,0);break;
 case 55:owner=RUNTIME_TARGET_CAMERA;memset(&r,0x5A,sizeof(r));m=r;CHECK(!App_MessagesReceive(APP_CHANNEL_CAMERA_COMMAND,&r));CHECK(memcmp(&r,&m,sizeof(r))==0);Trace(9,0,0,0);break;
 case 56:s_messages_attempted=false;fail_create=6;CHECK(App_MessagesInit()==SYS_ERR_INVALID_STATE);Trace(10,0,0,0);break;
 case 57:CHECK(App_MessagesInit()==SYS_ERR_INVALID_STATE);Trace(11,0,0,0);break;
 case 58:for(i=0;i<4;i++)CHECK(App_MessagesSend(APP_CHANNEL_CAMERA_COMMAND,&m)==0);m.command_id=88;CHECK(App_MessagesSend(APP_CHANNEL_USB_COMMAND,&m)==0);owner=RUNTIME_TARGET_USB;CHECK(App_MessagesReceive(APP_CHANNEL_USB_COMMAND,&r)&&r.command_id==88);Trace(12,0,0,0);break;
 case 59:owner=RUNTIME_TARGET_USB;CHECK(!App_MessagesReceive(APP_CHANNEL_CAMERA_COMMAND,&r));Trace(13,0,0,0);break;
 default:CHECK(false);
 }
 CHECK(g_failure==0);return 0;
}

#ifndef C1_BASELINE
//生成合法生命周期消息，便于穷举两种消费者和六种操作
static AppLifecycleMessage_t Life(uint32_t consumer,uint32_t op,uint32_t kind)
{AppLifecycleMessage_t m;uint32_t peer=consumer==1?APP_LIFECYCLE_USB:APP_LIFECYCLE_STORAGE;memset(&m,0,sizeof(m));m.version=1;m.kind=kind;m.operation=op;m.identity.session_id=op==1&&kind==1?0:9;m.identity.request_id=7;m.identity.consumer_id=consumer;m.identity.consumer_generation=3;m.source=op>=5?APP_LIFECYCLE_CAMERA:peer;m.target=op>=5?peer:APP_LIFECYCLE_CAMERA;if(kind==2){uint32_t t=m.source;m.source=m.target;m.target=t;}if(op==6&&kind==1)m.result=SYS_ERR_IO;return m;}
static SysResult_t Transport(void*ctx,const AppLifecycleMessage_t*m){return App_LifecycleMessagesSend(*(AppLifecycleChannel_t*)ctx,m);}
#endif

//新通道不执行业务，只验证真实队列、方向、权限和可靠提交
uint32_t C1_TransportRun(uint32_t id)
{
#ifndef C1_BASELINE
 AppLifecycleMessage_t m=Life(1,2,1),r,before;AppLifecycleChannel_t channel=APP_LIFECYCLE_CHANNEL_CAMERA;AppLifecyclePending_t tx;
 AppMessage_t old,old_reply;uint32_t i;memset(&tx,0,sizeof(tx));
 if(id<3){fail_create=id+1;CHECK(App_LifecycleMessagesInit()==SYS_ERR_NO_RESOURCE);CHECK(App_LifecycleMessagesInit()==SYS_ERR_INVALID_STATE);scheduler=taskSCHEDULER_RUNNING;owner=RUNTIME_TARGET_USB;CHECK(App_LifecycleMessagesSend(channel,&m)==SYS_ERR_INVALID_STATE);CHECK(!App_LifecycleMessagesReceive(channel,&r));return 0;}
 if(id==3){exception=1;CHECK(App_LifecycleMessagesInit()==SYS_ERR_INVALID_STATE);exception=0;CHECK(App_LifecycleMessagesInit()==0);return 0;}
 if(id==4){primask=1;CHECK(App_LifecycleMessagesInit()==SYS_ERR_INVALID_STATE);primask=0;CHECK(App_LifecycleMessagesInit()==0);return 0;}
 if(id==5){scheduler=taskSCHEDULER_RUNNING;CHECK(App_LifecycleMessagesInit()==SYS_ERR_INVALID_STATE);scheduler=taskSCHEDULER_NOT_STARTED;CHECK(App_LifecycleMessagesInit()==0);return 0;}
 CHECK(App_LifecycleMessagesInit()==0);CHECK(creates==3);CHECK(sizeof(s_lifecycle_storage)==672);CHECK(sizeof(s_lifecycle_queues)==216);CHECK(sizeof(s_lifecycle_handles)==12);scheduler=taskSCHEDULER_RUNNING;owner=RUNTIME_TARGET_USB;
 if(id>=6&&id<30)
 {
  uint32_t n=id-6;m=Life(1+n/12,1+(n%12)/2,1+n%2);channel=(AppLifecycleChannel_t)(m.target==APP_LIFECYCLE_CAMERA?0:m.target==APP_LIFECYCLE_USB?1:2);owner=Lifecycle_Task(m.source);
  CHECK(App_LifecycleMessagesSend(channel,&m)==0);memset(&m,0,sizeof(m));owner=Lifecycle_Task(Lifecycle_Receiver(channel));CHECK(App_LifecycleMessagesReceive(channel,&r));m=Life(1+n/12,1+(n%12)/2,1+n%2);CHECK(memcmp(&m,&r,sizeof(m))==0);CHECK(uxQueueMessagesWaiting(s_lifecycle_handles[channel])==0);return 0;
 }
 switch(id)
 {
 case 30:exception=1;CHECK(App_LifecycleMessagesSend(channel,&m)==SYS_ERR_INVALID_STATE);break;
 case 31:scheduler=taskSCHEDULER_SUSPENDED;CHECK(App_LifecycleMessagesSend(channel,&m)==SYS_ERR_INVALID_STATE);break;
 case 32:primask=1;CHECK(App_LifecycleMessagesSend(channel,&m)==SYS_ERR_INVALID_STATE);break;
 case 33:basepri=1;CHECK(App_LifecycleMessagesSend(channel,&m)==SYS_ERR_INVALID_STATE);break;
 case 34:owner=RUNTIME_TARGET_STORAGE;CHECK(App_LifecycleMessagesSend(channel,&m)==SYS_ERR_INVALID_STATE);break;
 case 35:CHECK(App_LifecycleMessagesSend(APP_LIFECYCLE_CHANNEL_STORAGE,&m)==SYS_ERR_INVALID_STATE);break;
 case 36:m.version++;CHECK(App_LifecycleMessagesSend(channel,&m)==SYS_ERR_INVALID_ARG);break;
 case 37:for(i=0;i<4;i++){m.identity.request_id=i+1;CHECK(App_LifecycleMessagesSend(channel,&m)==0);}CHECK(App_LifecycleMessagesSend(channel,&m)==SYS_ERR_NO_RESOURCE);owner=RUNTIME_TARGET_CAMERA;for(i=0;i<4;i++){CHECK(App_LifecycleMessagesReceive(channel,&r));CHECK(r.identity.request_id==i+1);}break;
 case 38:for(i=0;i<4;i++)CHECK(App_LifecycleMessagesSend(channel,&m)==0);CHECK(App_LifecycleStage(&tx,&m)==0);CHECK(App_LifecycleFlush(&tx,Transport,&channel)==SYS_ERR_NO_RESOURCE);CHECK(tx.phase==APP_LIFECYCLE_TX_READY);owner=RUNTIME_TARGET_CAMERA;CHECK(App_LifecycleMessagesReceive(channel,&r));owner=RUNTIME_TARGET_USB;CHECK(App_LifecycleFlush(&tx,Transport,&channel)==0);CHECK(tx.phase==APP_LIFECYCLE_TX_WAIT_REPLY);CHECK(App_LifecycleFlush(&tx,Transport,&channel)==SYS_ERR_BUSY);break;
 case 39:notify_fails=true;CHECK(App_LifecycleStage(&tx,&m)==0);CHECK(App_LifecycleFlush(&tx,Transport,&channel)==0);CHECK(App_LifecycleFlush(&tx,Transport,&channel)==SYS_ERR_BUSY);CHECK(uxQueueMessagesWaiting(s_lifecycle_handles[channel])==1);owner=RUNTIME_TARGET_CAMERA;CHECK(App_LifecycleMessagesReceive(channel,&r));CHECK(memcmp(&r,&m,sizeof(m))==0);break;
 case 40:CHECK(App_LifecycleMessagesSend(channel,&m)==0);before=m;r=before;owner=RUNTIME_TARGET_STORAGE;CHECK(!App_LifecycleMessagesReceive(channel,&r));CHECK(memcmp(&r,&before,sizeof(r))==0);break;
 case 41:owner=RUNTIME_TARGET_CAMERA;memset(&r,0x5A,sizeof(r));before=r;CHECK(!App_LifecycleMessagesReceive(channel,&r));CHECK(memcmp(&r,&before,sizeof(r))==0);break;
 case 42:CHECK(App_LifecycleMessagesSend(channel,&m)==0);posts=post_targets=0;owner=RUNTIME_TARGET_CAMERA;CHECK(App_LifecycleMessagesReceive(channel,&r));CHECK(posts==2&&post_targets==((1U<<RUNTIME_TARGET_USB)|(1U<<RUNTIME_TARGET_STORAGE)));break;
 case 43:m=Life(1,5,1);owner=RUNTIME_TARGET_CAMERA;CHECK(App_LifecycleMessagesSend(APP_LIFECYCLE_CHANNEL_USB,&m)==0);posts=post_targets=0;owner=RUNTIME_TARGET_USB;CHECK(App_LifecycleMessagesReceive(APP_LIFECYCLE_CHANNEL_USB,&r));CHECK(posts==1&&post_targets==(1U<<RUNTIME_TARGET_CAMERA));break;
 case 44:scheduler=taskSCHEDULER_NOT_STARTED;CHECK(App_MessagesInit()==0);scheduler=taskSCHEDULER_RUNNING;for(i=0;i<4;i++)CHECK(App_LifecycleMessagesSend(APP_LIFECYCLE_CHANNEL_CAMERA,&m)==0);owner=RUNTIME_TARGET_CONTROL;old=Legacy(123);CHECK(App_MessagesSend(APP_CHANNEL_CAMERA_COMMAND,&old)==0);owner=RUNTIME_TARGET_CAMERA;CHECK(App_MessagesReceive(APP_CHANNEL_CAMERA_COMMAND,&old_reply));CHECK(memcmp(&old,&old_reply,sizeof(old))==0);break;
 case 45:CHECK(App_LifecycleMessagesSend(APP_LIFECYCLE_CHANNEL_CAMERA,&m)==0);scheduler=taskSCHEDULER_NOT_STARTED;CHECK(App_MessagesInit()==0);scheduler=taskSCHEDULER_RUNNING;owner=RUNTIME_TARGET_CAMERA;CHECK(App_LifecycleMessagesReceive(APP_LIFECYCLE_CHANNEL_CAMERA,&r));CHECK(memcmp(&m,&r,sizeof(m))==0);break;
 case 46:CHECK(App_LifecycleMessagesInit()==SYS_ERR_INVALID_STATE);break;
 case 47:CHECK(App_LifecycleMessagesSend(APP_LIFECYCLE_CHANNEL_COUNT,&m)==SYS_ERR_INVALID_ARG);CHECK(App_LifecycleMessagesSend(APP_LIFECYCLE_CHANNEL_CAMERA,NULL)==SYS_ERR_INVALID_ARG);CHECK(!App_LifecycleMessagesReceive(APP_LIFECYCLE_CHANNEL_CAMERA,NULL));break;
 case 48:CHECK(App_LifecycleMessagesSend(APP_LIFECYCLE_CHANNEL_CAMERA,&m)==0);owner=RUNTIME_TARGET_CAMERA;primask=1;CHECK(!App_LifecycleMessagesReceive(APP_LIFECYCLE_CHANNEL_CAMERA,&r));CHECK(uxQueueMessagesWaiting(s_lifecycle_handles[0])==1);break;
 default:CHECK(false);
 }
 CHECK(g_failure==0);
#endif
 return 0;
}

//穷举每种合法生命周期消息与五个任务的发送或接收权限
uint32_t C1_PermissionRun(uint32_t id)
{
#ifndef C1_BASELINE
 AppLifecycleMessage_t m,r;AppLifecycleChannel_t channel;uint32_t n=id%120,variant=n/5,task=n%5;
 RuntimeEventTarget_t sender,receiver;bool allowed;
 CHECK(App_LifecycleMessagesInit()==0);scheduler=taskSCHEDULER_RUNNING;
 m=Life(1+variant/12,1+(variant%12)/2,1+variant%2);
 channel=(AppLifecycleChannel_t)(m.target==APP_LIFECYCLE_CAMERA?0:m.target==APP_LIFECYCLE_USB?1:2);
 sender=Lifecycle_Task(m.source);receiver=Lifecycle_Task(m.target);
 if(id<120)
 {
  owner=(RuntimeEventTarget_t)task;allowed=owner==sender;
  CHECK(App_LifecycleMessagesSend(channel,&m)==(allowed?SYS_OK:SYS_ERR_INVALID_STATE));
  CHECK(uxQueueMessagesWaiting(s_lifecycle_handles[channel])==(allowed?1U:0U));
 }
 else
 {
  owner=sender;CHECK(App_LifecycleMessagesSend(channel,&m)==0);owner=(RuntimeEventTarget_t)task;
  allowed=owner==receiver;CHECK(App_LifecycleMessagesReceive(channel,&r)==allowed);
  CHECK(uxQueueMessagesWaiting(s_lifecycle_handles[channel])==(allowed?0U:1U));
  if(allowed)CHECK(memcmp(&m,&r,sizeof(m))==0);
 }
#endif
 return 0;
}
