//复用C1的真实FreeRTOS队列和任务环境，增加可靠性交错验证
#include "../CameraLifecycleC1/transport_test.c"

//发送过程中尝试取消，只检查协议状态，不执行任何业务操作
static SysResult_t CancelDuringSend(void *context,const AppLifecycleMessage_t *message)
{
 AppLifecyclePending_t *pending=context;(void)message;
 if(pending->phase!=APP_LIFECYCLE_TX_SENDING ||
    App_LifecycleCancelReady(pending)!=SYS_ERR_INVALID_STATE || pending->phase!=APP_LIFECYCLE_TX_SENDING)
  return SYS_ERR_PROTOCOL;
 return SYS_ERR_IO;
}

//在真实队列中组合重复请求、待发ACK和退休拒绝
uint32_t C1_1_ReliabilityRun(uint32_t id)
{
 AppLifecycleMessage_t m=Life(1,APP_LIFECYCLE_ACQUIRE,APP_LIFECYCLE_REQUEST),reply,received,other;
 AppLifecyclePending_t request_tx,reply_tx,saved_tx;
 AppLifecycleReplay_t record,saved_record,pool[2];
 AppLifecycleChannel_t camera_channel=APP_LIFECYCLE_CHANNEL_CAMERA,usb_channel=APP_LIFECYCLE_CHANNEL_USB;
 uint32_t i,executions=0,sequence=99,saved_posts;
 memset(&request_tx,0,sizeof(request_tx));memset(&reply_tx,0,sizeof(reply_tx));
 memset(&record,0,sizeof(record));memset(pool,0,sizeof(pool));
 CHECK(App_LifecycleMessagesInit()==SYS_OK);scheduler=taskSCHEDULER_RUNNING;
 CHECK(App_LifecycleMakeReply(&m,9,SYS_OK,&reply)==SYS_OK);
 switch(id)
 {
 case 0:
  CHECK(App_LifecycleReplayBegin(&record,&m)==0);CHECK(App_LifecycleReplayComplete(&record,&reply)==0);saved_record=record;
  CHECK(App_LifecycleReplayRetire(&record)==SYS_ERR_NOT_SUPPORTED);CHECK(memcmp(&record,&saved_record,sizeof(record))==0);
  CHECK(App_LifecycleReplayGetReply(&record,&received)==0);CHECK(memcmp(&received,&reply,sizeof(reply))==0);break;
 case 1:
  CHECK(App_LifecycleReplayBegin(&record,&m)==0);executions++;CHECK(App_LifecycleReplayComplete(&record,&reply)==0);
  owner=RUNTIME_TARGET_CAMERA;CHECK(App_LifecycleStage(&reply_tx,&reply)==0);CHECK(App_LifecycleFlush(&reply_tx,Transport,&usb_channel)==0);
  CHECK(uxQueueMessagesWaiting(s_lifecycle_handles[usb_channel])==1);CHECK(App_LifecycleReplayRetire(&record)==SYS_ERR_NOT_SUPPORTED);
  CHECK(App_LifecycleReplayBegin(&record,&m)==SYS_ERR_ALREADY_EXISTS);CHECK(executions==1);
  owner=RUNTIME_TARGET_USB;CHECK(App_LifecycleMessagesReceive(usb_channel,&received));CHECK(App_LifecycleMatchesReply(&m,&received));break;
 case 2:
  CHECK(App_LifecycleReplayBegin(&record,&m)==0);CHECK(App_LifecycleReplayComplete(&record,&reply)==0);
  CHECK(App_LifecycleReplayRetire(&record)==SYS_ERR_NOT_SUPPORTED);CHECK(App_LifecycleReplayBegin(&record,&m)==SYS_ERR_ALREADY_EXISTS);
  CHECK(App_LifecycleReplayGetReply(&record,&received)==0);CHECK(memcmp(&received,&reply,sizeof(reply))==0);break;
 case 3:
  CHECK(App_LifecycleReplayBegin(&record,&m)==0);CHECK(App_LifecycleReplayComplete(&record,&reply)==0);saved_record=record;
  other=m;other.identity.session_id++;
  CHECK(!App_LifecycleMatchesCurrent(&m,other.identity.session_id,1,3));
  CHECK(App_LifecycleReplayBegin(&record,&other)==SYS_ERR_BUSY);CHECK(App_LifecycleReplayRetire(&record)==SYS_ERR_NOT_SUPPORTED);
  CHECK(memcmp(&record,&saved_record,sizeof(record))==0);CHECK(App_LifecycleMatchesCurrent(&other,10,1,3));break;
 case 4:
  other=m;other.identity.consumer_generation++;owner=RUNTIME_TARGET_USB;
  CHECK(App_LifecycleStage(&request_tx,&other)==0);CHECK(App_LifecycleFlush(&request_tx,Transport,&camera_channel)==0);saved_tx=request_tx;
  CHECK(App_LifecycleAcceptReply(&request_tx,&reply)==SYS_ERR_PROTOCOL);CHECK(memcmp(&request_tx,&saved_tx,sizeof(request_tx))==0);
  CHECK(App_LifecycleMakeReply(&other,9,0,&received)==0);CHECK(App_LifecycleAcceptReply(&request_tx,&received)==0);break;
 case 5:
  CHECK(App_LifecycleStage(&request_tx,&m)==0);CHECK(App_LifecycleCancelReady(&request_tx)==0);CHECK(App_LifecycleCancelReady(&request_tx)==SYS_ERR_INVALID_STATE);
  CHECK(App_LifecycleFlush(&request_tx,Transport,&camera_channel)==SYS_ERR_INVALID_STATE);CHECK(uxQueueMessagesWaiting(s_lifecycle_handles[camera_channel])==0);
  CHECK(App_LifecycleStage(&request_tx,&m)==0);owner=RUNTIME_TARGET_USB;CHECK(App_LifecycleFlush(&request_tx,Transport,&camera_channel)==0);
  CHECK(App_LifecycleFlush(&request_tx,Transport,&camera_channel)==SYS_ERR_BUSY);owner=RUNTIME_TARGET_CAMERA;
  CHECK(App_LifecycleMessagesReceive(camera_channel,&received));CHECK(App_LifecycleReplayBegin(&record,&received)==0);CHECK(App_LifecycleReplayBegin(&record,&received)==SYS_ERR_ALREADY_EXISTS);break;
 case 6:
  owner=RUNTIME_TARGET_USB;CHECK(App_LifecycleStage(&request_tx,&m)==0);CHECK(App_LifecycleFlush(&request_tx,Transport,&camera_channel)==0);saved_tx=request_tx;
  CHECK(App_LifecycleCancelReady(&request_tx)==SYS_ERR_INVALID_STATE);CHECK(App_LifecycleClear(&request_tx)==SYS_ERR_BUSY);CHECK(memcmp(&request_tx,&saved_tx,sizeof(request_tx))==0);break;
 case 7:
  CHECK(App_LifecycleStage(&request_tx,&m)==0);CHECK(App_LifecycleFlush(&request_tx,CancelDuringSend,&request_tx)==SYS_ERR_IO);
  CHECK(request_tx.phase==APP_LIFECYCLE_TX_READY);CHECK(App_LifecycleCancelReady(&request_tx)==0);break;
 case 8:
  owner=RUNTIME_TARGET_USB;CHECK(App_LifecycleStage(&request_tx,&m)==0);CHECK(App_LifecycleFlush(&request_tx,Transport,&camera_channel)==0);
  CHECK(App_LifecycleAcceptReply(&request_tx,&reply)==0);saved_tx=request_tx;CHECK(App_LifecycleCancelReady(&request_tx)==SYS_ERR_INVALID_STATE);CHECK(memcmp(&request_tx,&saved_tx,sizeof(request_tx))==0);break;
 case 9:
  owner=RUNTIME_TARGET_CAMERA;CHECK(App_LifecycleStage(&reply_tx,&reply)==0);CHECK(App_LifecycleFlush(&reply_tx,Transport,&usb_channel)==0);saved_tx=reply_tx;
  CHECK(App_LifecycleCancelReady(&reply_tx)==SYS_ERR_INVALID_STATE);CHECK(memcmp(&reply_tx,&saved_tx,sizeof(reply_tx))==0);break;
 case 10:
  CHECK(App_LifecycleStage(&reply_tx,&reply)==0);saved_tx=reply_tx;CHECK(App_LifecycleCancelReady(&reply_tx)==SYS_ERR_INVALID_STATE);CHECK(memcmp(&reply_tx,&saved_tx,sizeof(reply_tx))==0);break;
 case 11:CHECK(App_LifecycleCancelReady(NULL)==SYS_ERR_INVALID_ARG);break;
 case 12:
  saved_tx=request_tx;CHECK(App_LifecycleCancelReady(&request_tx)==SYS_ERR_INVALID_STATE);CHECK(memcmp(&request_tx,&saved_tx,sizeof(request_tx))==0);
  CHECK(App_LifecycleStage(&request_tx,&m)==0);CHECK(App_LifecycleCancelReady(&request_tx)==0);CHECK(memcmp(&request_tx,&saved_tx,sizeof(request_tx))==0);break;
 case 13:
  owner=RUNTIME_TARGET_USB;for(i=0;i<4;i++){other=m;other.identity.request_id=100+i;CHECK(App_LifecycleMessagesSend(camera_channel,&other)==0);}
  CHECK(App_LifecycleStage(&request_tx,&m)==0);CHECK(App_LifecycleFlush(&request_tx,Transport,&camera_channel)==SYS_ERR_NO_RESOURCE);CHECK(App_LifecycleCancelReady(&request_tx)==0);
  owner=RUNTIME_TARGET_CAMERA;for(i=0;i<4;i++){CHECK(App_LifecycleMessagesReceive(camera_channel,&received));CHECK(received.identity.request_id==100+i);}CHECK(!App_LifecycleMessagesReceive(camera_channel,&received));break;
 case 14:
  owner=RUNTIME_TARGET_USB;CHECK(App_LifecycleStage(&request_tx,&m)==0);CHECK(App_LifecycleFlush(&request_tx,Transport,&camera_channel)==0);
  owner=RUNTIME_TARGET_CAMERA;CHECK(App_LifecycleMessagesReceive(camera_channel,&received));CHECK(App_LifecycleReplayBegin(&record,&received)==0);executions++;
  CHECK(App_LifecycleReplayComplete(&record,&reply)==0);
  for(i=0;i<4;i++){other=reply;other.identity.request_id=100+i;CHECK(App_LifecycleMessagesSend(usb_channel,&other)==0);}
  CHECK(App_LifecycleStage(&reply_tx,&reply)==0);CHECK(App_LifecycleFlush(&reply_tx,Transport,&usb_channel)==SYS_ERR_NO_RESOURCE);CHECK(App_LifecycleReplayRetire(&record)==SYS_ERR_NOT_SUPPORTED);
  owner=RUNTIME_TARGET_USB;CHECK(App_LifecycleMessagesSend(camera_channel,&m)==0);
  owner=RUNTIME_TARGET_CAMERA;CHECK(App_LifecycleMessagesReceive(camera_channel,&received));CHECK(App_LifecycleReplayBegin(&record,&received)==SYS_ERR_ALREADY_EXISTS);CHECK(executions==1);
  CHECK(App_LifecycleReplayGetReply(&record,&received)==0);CHECK(App_LifecycleStage(&reply_tx,&received)==SYS_ERR_ALREADY_EXISTS);
  owner=RUNTIME_TARGET_USB;CHECK(App_LifecycleMessagesReceive(usb_channel,&received));
  owner=RUNTIME_TARGET_CAMERA;CHECK(App_LifecycleFlush(&reply_tx,Transport,&usb_channel)==0);CHECK(App_LifecycleClear(&reply_tx)==0);CHECK(App_LifecycleReplayRetire(&record)==SYS_ERR_NOT_SUPPORTED);
  owner=RUNTIME_TARGET_USB;for(i=1;i<4;i++)CHECK(App_LifecycleMessagesReceive(usb_channel,&received));CHECK(App_LifecycleMessagesReceive(usb_channel,&received));
  CHECK(App_LifecycleAcceptReply(&request_tx,&received)==0);CHECK(App_LifecycleReplayBegin(&record,&m)==SYS_ERR_ALREADY_EXISTS);break;
 case 15:
  for(i=0;i<2;i++){other=m;other.identity.request_id+=i;CHECK(App_LifecycleReplayBegin(&pool[i],&other)==0);CHECK(App_LifecycleMakeReply(&other,9,0,&received)==0);CHECK(App_LifecycleReplayComplete(&pool[i],&received)==0);}
  other=m;other.identity.request_id+=2;
  for(i=0;i<2;i++){saved_record=pool[i];CHECK(App_LifecycleReplayBegin(&pool[i],&other)==SYS_ERR_BUSY);CHECK(App_LifecycleReplayRetire(&pool[i])==SYS_ERR_NOT_SUPPORTED);CHECK(memcmp(&pool[i],&saved_record,sizeof(saved_record))==0);CHECK(App_LifecycleReplayGetReply(&pool[i],&received)==0);}break;
 case 16:
  CHECK(App_LifecycleReplayBegin(&record,&m)==0);reply.result=SYS_ERR_IO;CHECK(App_LifecycleReplayComplete(&record,&reply)==0);CHECK(App_LifecycleReplayRetire(&record)==SYS_ERR_NOT_SUPPORTED);
  other=reply;other.result=SYS_OK;CHECK(App_LifecycleReplayComplete(&record,&other)==SYS_ERR_PROTOCOL);CHECK(record.reply.result==SYS_ERR_IO);break;
 case 17:
  other=m;other.identity.request_id++;owner=RUNTIME_TARGET_USB;CHECK(App_LifecycleStage(&request_tx,&other)==0);CHECK(App_LifecycleFlush(&request_tx,Transport,&camera_channel)==0);saved_tx=request_tx;
  CHECK(App_LifecycleAcceptReply(&request_tx,&reply)==SYS_ERR_PROTOCOL);CHECK(memcmp(&request_tx,&saved_tx,sizeof(request_tx))==0);break;
 case 18:
  other=m;saved_posts=posts;CHECK(App_LifecycleStage(&request_tx,&m)==0);CHECK(App_LifecycleCancelReady(&request_tx)==0);
  CHECK(memcmp(&m,&other,sizeof(m))==0&&sequence==99&&posts==saved_posts);break;
 case 19:
  CHECK(App_LifecycleStage(&request_tx,&m)==0);CHECK(App_LifecycleCancelReady(&request_tx)==0);CHECK(App_LifecycleStage(&request_tx,&m)==0);
  owner=RUNTIME_TARGET_USB;CHECK(App_LifecycleFlush(&request_tx,Transport,&camera_channel)==0);CHECK(uxQueueMessagesWaiting(s_lifecycle_handles[camera_channel])==1);break;
 case 20:
  CHECK(App_LifecycleReplayRetire(NULL)==SYS_ERR_INVALID_ARG);CHECK(App_LifecycleReplayRetire(&record)==SYS_ERR_BUSY);
  CHECK(App_LifecycleReplayBegin(&record,&m)==0);saved_record=record;CHECK(App_LifecycleReplayRetire(&record)==SYS_ERR_BUSY);CHECK(memcmp(&record,&saved_record,sizeof(record))==0);break;
 case 21:
  other=m;other.identity.session_id++;CHECK(!App_LifecycleMatchesReply(&other,&reply));other=m;other.identity.consumer_generation++;CHECK(!App_LifecycleMatchesReply(&other,&reply));break;
 case 22:
  CHECK(App_LifecycleReplayBegin(&record,&m)==0);CHECK(App_LifecycleReplayComplete(&record,&reply)==0);saved_record=record;
  CHECK(App_LifecycleReplayComplete(&record,&reply)==SYS_ERR_ALREADY_EXISTS);CHECK(memcmp(&record,&saved_record,sizeof(record))==0);break;
 case 23:
  owner=RUNTIME_TARGET_USB;CHECK(App_LifecycleStage(&request_tx,&m)==0);CHECK(App_LifecycleFlush(&request_tx,Transport,&camera_channel)==0);reply.result=SYS_ERR_BUSY;
  CHECK(App_LifecycleAcceptReply(&request_tx,&reply)==0);CHECK(App_LifecycleCancelReady(&request_tx)==SYS_ERR_INVALID_STATE);CHECK(request_tx.reply.result==SYS_ERR_BUSY);break;
 default:CHECK(false);
 }
 CHECK(g_failure==0);return 0;
}
