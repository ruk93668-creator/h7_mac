#include "../CameraModuleB1/behavior_test.c"
#include "app_camera_consumer.h"
static AppCameraConsumer_t clients[2];
static AppLifecycleMessage_t inbox[4];
static uint32_t in_count,in_head,self_wakes;
SysResult_t RuntimeEvent_Post(RuntimeEventTarget_t target,uint32_t events)
{if(target==RUNTIME_TARGET_CAMERA&&events==RUNTIME_EVENT_WAKE)self_wakes++;return SYS_OK;}
bool App_LifecycleMessagesReceive(AppLifecycleChannel_t c,AppLifecycleMessage_t*m)
{(void)c;if(!in_count)return false;*m=inbox[in_head];in_head=(in_head+1)%4;in_count--;return true;}
SysResult_t App_LifecycleMessagesSend(AppLifecycleChannel_t c,const AppLifecycleMessage_t*m)
{
 if(c==APP_LIFECYCLE_CHANNEL_CAMERA){if(in_count==4)return SYS_ERR_NO_RESOURCE;inbox[(in_head+in_count++)%4]=*m;return SYS_OK;}
 return App_CameraConsumerReceive(&clients[m->identity.consumer_id-1],m);
}
static SysResult_t Send(void*x,const AppLifecycleMessage_t*m){(void)x;return App_LifecycleMessagesSend(APP_LIFECYCLE_CHANNEL_CAMERA,m);}
static void Step(void)
{
 uint32_t i;
 for(i=0;i<2;i++)(void)App_CameraConsumerFlush(&clients[i],Send,NULL);
 CameraTask_Process(RUNTIME_EVENT_BOOT_READY);
 for(i=0;i<2;i++)(void)App_CameraConsumerProcess(&clients[i]);
}
#define VERIFY(x) do{if(!(x)){g_failure=__LINE__;return g_failure;}}while(0)
int CameraTaskIntegration_Run(uint32_t id)
{
 uint32_t i;AppMessage_t legacy={APP_MESSAGE_STOP};
 sensor.base.state=DEVICE_STATE_READY;
 VERIFY(App_CameraQualificationPrepare()==SYS_OK);s_lifecycle_ready=true;
 VERIFY(App_CameraConsumerInit(&clients[0],APP_LIFECYCLE_USB,1)==SYS_OK);
 VERIFY(App_CameraConsumerInit(&clients[1],APP_LIFECYCLE_STORAGE,2)==SYS_OK);
 if(id==0){Step();VERIFY(!s_camera.running&&!s_camera.prepared);return 0;}
 if(id==5)
 {
  VERIFY(App_CameraConsumerBegin(&clients[0])==SYS_OK);
  VERIFY(App_CameraConsumerBegin(&clients[1])==SYS_OK);
  for(i=0;i<8;i++)Step();
  VERIFY(clients[0].granted==1&&clients[1].granted==1&&s_camera.running&&self_wakes==4U);
  VERIFY(s_camera.qualifications.entries[0].phase==APP_QUAL_ACTIVE&&s_camera.qualifications.entries[1].phase==APP_QUAL_ACTIVE);
  return 0;
 }
 VERIFY(App_CameraConsumerBegin(&clients[0])==SYS_OK);for(i=0;i<4;i++)Step();
 VERIFY(s_camera.running&&clients[0].granted==1&&s_camera.qualifications.entries[0].phase==APP_QUAL_ACTIVE);
 if(id==1)return 0;
 if(id==2){VERIFY(App_CameraConsumerBegin(&clients[1])==SYS_OK);for(i=0;i<4;i++)Step();VERIFY(clients[1].granted==1&&s_camera.running);return 0;}
 if(id==3){App_CameraHandleCommand(&legacy);VERIFY(!s_camera.stopping&&s_camera.ack.result==SYS_ERR_NOT_SUPPORTED);return 0;}
 if(id==4){VERIFY(App_CameraConsumerEnd(&clients[0],SYS_OK,SYS_OK)==SYS_OK);for(i=0;i<8;i++)Step();VERIFY(s_camera.running&&clients[0].granted==1&&s_camera.qualifications.entries[0].phase==APP_QUAL_RELEASING);return 0;}
 VERIFY(0);return g_failure;
}
