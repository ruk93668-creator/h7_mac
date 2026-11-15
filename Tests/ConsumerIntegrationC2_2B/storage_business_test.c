#include "../StorageModuleB3/behavior_test.c"
static AppLifecycleMessage_t captured;
static uint32_t sent_requests;
static SysResult_t send_error;
bool App_LifecycleMessagesReceive(AppLifecycleChannel_t c,AppLifecycleMessage_t*m)
{ (void)c;(void)m;return false; }
SysResult_t App_LifecycleMessagesSend(AppLifecycleChannel_t c,const AppLifecycleMessage_t*m)
{ (void)c;if(send_error!=SYS_OK)return send_error;captured=*m;sent_requests++;return SYS_OK; }
static SysResult_t Send(void*x,const AppLifecycleMessage_t*m)
{ (void)x;return App_LifecycleMessagesSend(APP_LIFECYCLE_CHANNEL_CAMERA,m); }
static void Step(void){Process();(void)App_StorageFlushLifecycle(Send,NULL);}
static void Reply(SysResult_t result)
{
 AppLifecycleMessage_t reply;
 if(App_LifecycleMakeReply(&captured,1U,result,&reply)!=SYS_OK){g_failure=__LINE__;return;}
 if(App_StorageReceiveLifecycle(&reply)!=SYS_OK){g_failure=__LINE__;return;}
 Step();
}
#define VERIFY(x) do{if(!(x)){g_failure=__LINE__;return g_failure;}}while(0)
int StorageBusiness_Run(uint32_t id)
{
 VERIFY(App_StoragePrepare(&storage_config)==SYS_OK);
 VERIFY((id==3?App_StorageRequestSnapshot():App_StorageStartRecording())==SYS_OK);Step();
 VERIFY(!s_mounted&&!s_accepting&&fs_state==1&&captured.operation==APP_LIFECYCLE_SESSION_QUERY);
 if(id==8){AppLifecycleMessage_t reply;VERIFY(App_LifecycleMakeReply(&captured,1,SYS_OK,&reply)==SYS_OK);owner=RUNTIME_TARGET_CAMERA;VERIFY(App_StorageReceiveLifecycle(&reply)==SYS_ERR_INVALID_STATE&&s_camera_consumer.phase==APP_CONSUMER_QUERY);return 0;}
 if(id==0)return 0;
 if(id==1){Reply(SYS_OK);Reply(SYS_ERR_IO);VERIFY(s_state==STORAGE_FAULT&&!s_mounted&&s_camera_consumer.granted==0);return 0;}
 if(id==5){VERIFY(App_StorageStop()==SYS_OK);Step();Reply(SYS_OK);Reply(SYS_OK);VERIFY(!s_mounted&&!s_accepting&&s_camera_consumer.phase==APP_CONSUMER_RELEASE);return 0;}
 if(id==4)fail_call=30;
 Reply(SYS_OK);VERIFY(!s_mounted&&!s_accepting);Reply(SYS_OK);
 if(id==4){VERIFY(s_state==STORAGE_FAULT&&s_camera_consumer.granted==1&&s_camera_consumer.phase==APP_CONSUMER_RELEASE);return 0;}
 VERIFY(s_mounted&&s_accepting&&bad_owner==0);
 if(id==2){VERIFY(Offer(false,false)==SYS_OK);Step();VERIFY(s_business.saved_frames==1&&s_slots[0].state==SLOT_RETURN_PENDING);VERIFY(App_StorageStop()==SYS_OK);Step();VERIFY(s_camera_consumer.phase==APP_CONSUMER_GRANTED);Returns();Step();VERIFY(s_state==STORAGE_IDLE&&s_camera_consumer.phase==APP_CONSUMER_RELEASE&&s_camera_consumer.granted==1);return 0;}
 if(id==3){VERIFY(Offer(true,false)==SYS_OK);Step();VERIFY(s_state==STORAGE_IDLE&&s_business.saved_frames==1&&s_slots[0].state==SLOT_RETURN_PENDING);VERIFY(s_camera_consumer.phase==APP_CONSUMER_GRANTED);Returns();Step();VERIFY(s_camera_consumer.phase==APP_CONSUMER_RELEASE);return 0;}
 if(id==6){VERIFY(App_StorageStartRecording()==SYS_OK);Step();VERIFY(g_storage_status.result==SYS_ERR_BUSY&&s_accepting);return 0;}
 if(id==7){VERIFY(Offer(false,false)==SYS_OK);fail_call=36;Step();VERIFY(s_state==STORAGE_FAULT&&s_camera_consumer.granted==1&&s_slots[0].state==SLOT_RETURN_PENDING);Returns();Step();VERIFY(s_camera_consumer.phase==APP_CONSUMER_RELEASE);return 0;}
 VERIFY(0);return g_failure;
}
