//执行真实普通C协议代码，不模拟业务状态或硬件
#include "app_camera_lifecycle.h"
#include <string.h>
volatile uint32_t g_failure;
#define CHECK(x) do {if(!(x)){g_failure=__LINE__;return __LINE__;}} while(0)

//生成两种消费者各方向的合法协议原文
static AppLifecycleMessage_t Message(uint32_t consumer,uint32_t operation,uint32_t kind)
{
 AppLifecycleMessage_t m;uint32_t peer=consumer==APP_CONSUMER_USB?APP_LIFECYCLE_USB:APP_LIFECYCLE_STORAGE;
 memset(&m,0,sizeof(m));m.version=APP_LIFECYCLE_VERSION;m.kind=kind;m.operation=operation;
 m.identity.session_id=operation==APP_LIFECYCLE_SESSION_QUERY&&kind==APP_LIFECYCLE_REQUEST?0:9;
 m.identity.request_id=7;m.identity.consumer_id=consumer;m.identity.consumer_generation=3;
 if(operation>=APP_LIFECYCLE_RELEASE_REQUEST){m.source=APP_LIFECYCLE_CAMERA;m.target=peer;}else{m.source=peer;m.target=APP_LIFECYCLE_CAMERA;}
 if(kind==APP_LIFECYCLE_REPLY){uint32_t t=m.source;m.source=m.target;m.target=t;}
 if(operation==APP_LIFECYCLE_FAULT_NOTICE&&kind==APP_LIFECYCLE_REQUEST)m.result=SYS_ERR_IO;
 return m;
}

//模拟传输结果并核对每轮提交是否沿用同一份原文
static uint32_t sends;
static SysResult_t send_result;
static AppLifecycleMessage_t sent;
static SysResult_t Send(void *ctx,const AppLifecycleMessage_t *m)
{(void)ctx;sent=*m;sends++;return send_result;}

//在回调中尝试重入，外层待发记录必须保持唯一
static SysResult_t Reenter(void *ctx,const AppLifecycleMessage_t *m)
{AppLifecyclePending_t*p=ctx;(void)m;if(App_LifecycleFlush(p,Send,NULL)!=SYS_ERR_BUSY)return SYS_ERR_PROTOCOL;return SYS_OK;}

//每次由模拟器重新清零内存，独立检查一种协议能力
uint32_t C1_ProtocolRun(uint32_t id)
{
 AppLifecycleMessage_t m=Message(1,APP_LIFECYCLE_ACQUIRE,APP_LIFECYCLE_REQUEST),r,before;
 AppLifecyclePending_t tx,copy;AppLifecycleReplay_t replay;uint32_t sequence=0,first=99;
 memset(&tx,0,sizeof(tx));memset(&replay,0,sizeof(replay));
 if(id<24)
 {
  m=Message(1+id/12,1+(id%12)/2,1+id%2);CHECK(App_LifecycleValidate(&m));
  before=m;m.version++;CHECK(!App_LifecycleValidate(&m));m=before;m.kind=0;CHECK(!App_LifecycleValidate(&m));
  m=before;m.operation=0;CHECK(!App_LifecycleValidate(&m));m=before;m.source=99;CHECK(!App_LifecycleValidate(&m));
  m=before;m.target=99;CHECK(!App_LifecycleValidate(&m));m=before;m.identity.request_id=0;CHECK(!App_LifecycleValidate(&m));
  m=before;m.identity.consumer_generation=0;CHECK(!App_LifecycleValidate(&m));m=before;m.identity.consumer_id=0;CHECK(!App_LifecycleValidate(&m));
  m=before;m.completion_flags=0x100;CHECK(!App_LifecycleValidate(&m));m=before;m.result=1;CHECK(!App_LifecycleValidate(&m));
  return 0;
 }
 CHECK(App_LifecycleMakeReply(&m,9,SYS_OK,&r)==SYS_OK);
 switch(id)
 {
 case 24:CHECK(!App_LifecycleValidate(NULL));CHECK(!App_LifecycleMatchesReply(NULL,&r));break;
 case 25:CHECK(sizeof(m)==56&&sizeof(AppLifecycleIdentity_t)==16);CHECK(offsetof(AppLifecycleMessage_t,cleanup_result)==52);break;
 case 26:m=Message(1,1,1);CHECK(App_LifecycleMakeReply(&m,55,SYS_OK,&r)==SYS_OK);CHECK(App_LifecycleMatchesReply(&m,&r));break;
 case 27:m=Message(1,1,1);CHECK(App_LifecycleMakeReply(&m,0,SYS_ERR_BUSY,&r)==SYS_OK);CHECK(App_LifecycleMatchesReply(&m,&r));break;
 case 28:r.identity.session_id++;CHECK(!App_LifecycleMatchesReply(&m,&r));break;
 case 29:r.identity.request_id++;CHECK(!App_LifecycleMatchesReply(&m,&r));break;
 case 30:r.identity.consumer_id=2;r.target=APP_LIFECYCLE_STORAGE;CHECK(!App_LifecycleMatchesReply(&m,&r));break;
 case 31:r.identity.consumer_generation++;CHECK(!App_LifecycleMatchesReply(&m,&r));break;
 case 32:r.source=APP_LIFECYCLE_USB;CHECK(!App_LifecycleMatchesReply(&m,&r));break;
 case 33:r.operation=APP_LIFECYCLE_RELEASE;CHECK(!App_LifecycleMatchesReply(&m,&r));break;
 case 34:m.operation=APP_LIFECYCLE_RELEASE;m.related_request_id=8;CHECK(App_LifecycleMakeReply(&m,9,0,&r)==SYS_OK);r.related_request_id++;CHECK(!App_LifecycleMatchesReply(&m,&r));break;
 case 35:CHECK(App_LifecycleMatchesCurrent(&m,9,1,3));CHECK(!App_LifecycleMatchesCurrent(&m,8,1,3));CHECK(!App_LifecycleMatchesCurrent(&m,9,2,3));CHECK(!App_LifecycleMatchesCurrent(&m,9,1,4));break;
 case 36:CHECK(App_LifecycleAllocateIds(&sequence,3,&first)==0);CHECK(first==1&&sequence==3);CHECK(App_LifecycleAllocateIds(&sequence,1,&first)==0&&first==4);break;
 case 37:sequence=UINT32_MAX-1;CHECK(App_LifecycleAllocateIds(&sequence,2,&first)==SYS_ERR_NO_RESOURCE);CHECK(sequence==UINT32_MAX-1&&first==99);break;
 case 38:sequence=UINT32_MAX-1;CHECK(App_LifecycleAllocateIds(&sequence,1,&first)==0&&first==UINT32_MAX);CHECK(App_LifecycleAllocateIds(&sequence,1,&first)==SYS_ERR_NO_RESOURCE&&sequence==UINT32_MAX);break;
 case 39:CHECK(App_LifecycleAllocateIds(NULL,1,&first)==SYS_ERR_INVALID_ARG);CHECK(App_LifecycleAllocateIds(&sequence,0,&first)==SYS_ERR_INVALID_ARG);CHECK(App_LifecycleAllocateIds(&sequence,1,&sequence)==SYS_ERR_INVALID_ARG);break;
 case 40:case 41:case 42:case 43:
  CHECK(App_LifecycleStage(&tx,&m)==0);copy=tx;send_result=id==40?SYS_ERR_NO_RESOURCE:id==41?SYS_ERR_BUSY:id==42?SYS_ERR_TIMEOUT:SYS_ERR_IO;
  CHECK(App_LifecycleFlush(&tx,Send,NULL)==send_result);CHECK(memcmp(&copy,&tx,sizeof(tx))==0);CHECK(memcmp(&sent,&m,sizeof(m))==0);
  send_result=0;CHECK(App_LifecycleFlush(&tx,Send,NULL)==0);CHECK(sends==2&&tx.phase==APP_LIFECYCLE_TX_WAIT_REPLY);break;
 case 44:CHECK(App_LifecycleStage(&tx,&m)==0);CHECK(App_LifecycleFlush(&tx,Send,NULL)==0);CHECK(App_LifecycleClear(&tx)==SYS_ERR_BUSY);CHECK(App_LifecycleFlush(&tx,Send,NULL)==SYS_ERR_BUSY&&sends==1);break;
 case 45:CHECK(App_LifecycleStage(&tx,&m)==0);CHECK(App_LifecycleAcceptReply(&tx,&r)==SYS_ERR_INVALID_STATE);CHECK(tx.phase==APP_LIFECYCLE_TX_READY);break;
 case 46:CHECK(App_LifecycleStage(&tx,&m)==0);CHECK(App_LifecycleFlush(&tx,Send,NULL)==0);CHECK(App_LifecycleAcceptReply(&tx,&r)==0);CHECK(App_LifecycleClear(&tx)==0);CHECK(tx.phase==0);break;
 case 47:CHECK(App_LifecycleStage(&tx,&m)==0);CHECK(App_LifecycleFlush(&tx,Send,NULL)==0);copy=tx;r.identity.consumer_generation++;CHECK(App_LifecycleAcceptReply(&tx,&r)==SYS_ERR_PROTOCOL);CHECK(memcmp(&tx,&copy,sizeof(tx))==0);break;
 case 48:CHECK(App_LifecycleMakeReply(&m,9,SYS_ERR_BUSY,&r)==0);CHECK(App_LifecycleStage(&tx,&m)==0);CHECK(App_LifecycleFlush(&tx,Send,NULL)==0);CHECK(App_LifecycleAcceptReply(&tx,&r)==0);CHECK(tx.reply.result==SYS_ERR_BUSY&&tx.phase==APP_LIFECYCLE_TX_COMPLETE);break;
 case 49:CHECK(App_LifecycleStage(&tx,&r)==0);CHECK(App_LifecycleFlush(&tx,Send,NULL)==0);CHECK(tx.phase==APP_LIFECYCLE_TX_COMMITTED);CHECK(App_LifecycleClear(&tx)==0);break;
 case 50:CHECK(App_LifecycleStage(&tx,&m)==0);CHECK(App_LifecycleStage(&tx,&m)==SYS_ERR_ALREADY_EXISTS);m.identity.request_id++;CHECK(App_LifecycleStage(&tx,&m)==SYS_ERR_BUSY);CHECK(tx.message.identity.request_id==7);break;
 case 51:CHECK(App_LifecycleStage(&tx,&m)==0);CHECK(App_LifecycleFlush(&tx,Reenter,&tx)==0);CHECK(tx.phase==APP_LIFECYCLE_TX_WAIT_REPLY);break;
 case 52:CHECK(App_LifecycleStage(&tx,&m)==0);CHECK(App_LifecycleFlush(&tx,Send,NULL)==0);CHECK(App_LifecycleAcceptReply(&tx,&r)==0);CHECK(App_LifecycleAcceptReply(&tx,&r)==SYS_ERR_INVALID_STATE);break;
 case 53:CHECK(App_LifecycleReplayBegin(&replay,&m)==0);CHECK(App_LifecycleReplayBegin(&replay,&m)==SYS_ERR_ALREADY_EXISTS);CHECK(App_LifecycleReplayGetReply(&replay,&r)==SYS_ERR_NOT_FOUND);break;
 case 54:m.operation=APP_LIFECYCLE_RELEASE;CHECK(App_LifecycleReplayBegin(&replay,&m)==0);m.business_result=SYS_ERR_IO;CHECK(App_LifecycleReplayBegin(&replay,&m)==SYS_ERR_PROTOCOL);break;
 case 55:CHECK(App_LifecycleReplayBegin(&replay,&m)==0);r.result=SYS_ERR_IO;CHECK(App_LifecycleReplayComplete(&replay,&r)==0);CHECK(App_LifecycleReplayComplete(&replay,&r)==SYS_ERR_ALREADY_EXISTS);r.result=0;CHECK(App_LifecycleReplayComplete(&replay,&r)==SYS_ERR_PROTOCOL);CHECK(replay.reply.result==SYS_ERR_IO);break;
 case 56:CHECK(App_LifecycleReplayBegin(&replay,&m)==0);CHECK(App_LifecycleReplayRetire(&replay)==SYS_ERR_BUSY);CHECK(App_LifecycleReplayComplete(&replay,&r)==0);CHECK(App_LifecycleReplayRetire(&replay)==SYS_ERR_NOT_SUPPORTED);CHECK(App_LifecycleReplayGetReply(&replay,&before)==0);CHECK(memcmp(&before,&r,sizeof(r))==0);break;
 case 57:CHECK(App_LifecycleReplayBegin(&replay,&m)==0);CHECK(App_LifecycleReplayComplete(&replay,&r)==0);before=r;CHECK(App_LifecycleReplayGetReply(&replay,&r)==0);CHECK(memcmp(&r,&before,sizeof(r))==0);CHECK(replay.completed==1);break;
 case 58:m.operation=APP_LIFECYCLE_RELEASE;m.completion_flags=APP_LIFECYCLE_QUALIFICATION_RELEASED;CHECK(!App_LifecycleValidate(&m));break;
 case 59:m.completion_flags=APP_LIFECYCLE_HARDWARE_STOPPED;CHECK(!App_LifecycleValidate(&m));break;
 case 60:m.operation=APP_LIFECYCLE_RELEASE;m.completion_flags=APP_LIFECYCLE_INPUT_CLOSED|APP_LIFECYCLE_WORK_QUIESCENT|APP_LIFECYCLE_FRAMES_DRAINED;m.business_result=SYS_ERR_IO;m.cleanup_result=SYS_ERR_BUSY;CHECK(App_LifecycleValidate(&m));CHECK(App_LifecycleMakeReply(&m,9,0,&r)==0);r.completion_flags=APP_LIFECYCLE_QUALIFICATION_RELEASED;CHECK(App_LifecycleValidate(&r));CHECK(r.business_result==SYS_ERR_IO&&r.cleanup_result==SYS_ERR_BUSY);break;
 case 61:r.result=1;CHECK(!App_LifecycleValidate(&r));r.result=0;r.business_result=1;CHECK(!App_LifecycleValidate(&r));break;
 case 62:memset(&r,0x5A,sizeof(r));before=r;CHECK(App_LifecycleMakeReply(&m,10,0,&r)==SYS_ERR_INVALID_ARG);CHECK(memcmp(&r,&before,sizeof(r))==0);break;
 case 63:CHECK(App_LifecycleMakeReply(&m,9,0,&m)==0);CHECK(m.kind==APP_LIFECYCLE_REPLY);break;
 case 64:m=Message(1,1,1);CHECK(App_LifecycleMakeReply(&m,0,0,&r)==SYS_ERR_INVALID_ARG);break;
 case 65:CHECK(App_LifecycleReplayBegin(&replay,&m)==0);m.identity.request_id++;CHECK(App_LifecycleReplayBegin(&replay,&m)==SYS_ERR_BUSY);CHECK(replay.request.identity.request_id==7);break;
 case 66:m.operation=APP_LIFECYCLE_RELEASE;m.related_request_id=17;CHECK(App_LifecycleMakeReply(&m,9,0,&r)==0);CHECK(r.related_request_id==17);break;
 case 67:copy=tx;m.version=99;CHECK(App_LifecycleStage(&tx,&m)==SYS_ERR_INVALID_ARG);CHECK(memcmp(&tx,&copy,sizeof(tx))==0);break;
 case 68:CHECK(App_LifecycleReplayBegin(NULL,&m)==SYS_ERR_INVALID_ARG);CHECK(App_LifecycleClear(NULL)==SYS_ERR_INVALID_ARG);CHECK(App_LifecycleFlush(NULL,Send,NULL)==SYS_ERR_INVALID_ARG);break;
 case 69:CHECK(!App_LifecycleMatchesCurrent(NULL,9,1,3));CHECK(!App_LifecycleMatchesCurrent(&m,0,1,3));break;
 default:CHECK(false);
 }
 return 0;
}
