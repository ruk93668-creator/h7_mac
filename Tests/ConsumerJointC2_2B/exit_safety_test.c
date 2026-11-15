//独立核心契约桩；不与真实Camera适配器共用，不宣称实板或真实帧归还
#include "app_camera_qualification.h"
#include <string.h>
//独立核心桩没有真实任务适配入口；不调用App_Camera包装层
uint32_t Platform_SyncGetException(void){return 0;}
volatile uint32_t g_failure;
static AppCameraQualification_t core;
static AppLifecycleTxContext_t tx[2];
static AppLifecycleMessage_t request[2],reply;
static uint32_t session,generation[2],generation_counter[2],hw,starts,stop_requests,release_checks,idle_checks;
static SysResult_t drain=SYS_ERR_BUSY,idle_result=SYS_ERR_BUSY;
#define CK(x) do{if(!(x)&&!g_failure)g_failure=__LINE__;}while(0)
//硬件状态为核心单元桩，只有真实ACQUIRE调用Start后才切换RUNNING
static uint32_t State(void*p){(void)p;return hw;}
static SysResult_t Start(void*p){(void)p;starts++;hw=APP_RESOURCE_RUNNING;return SYS_OK;}
//禁止测试伪造停止结果；这些测试应在证据门控处阻止stop请求
static SysResult_t RequestStop(void*p,const AppLifecycleIdentity_t*i){(void)p;(void)i;stop_requests++;return SYS_ERR_NOT_SUPPORTED;}
static SysResult_t ReadStop(void*p,const AppLifecycleIdentity_t*i,AppQualificationStopResult_t*r){(void)p;(void)i;(void)r;return SYS_ERR_NOT_FOUND;}
static SysResult_t ConsumeStop(void*p,const AppLifecycleIdentity_t*i){(void)p;(void)i;return SYS_ERR_NOT_SUPPORTED;}
//明确标识测试证据注入，只验证核心不把缺失或失败证据当作成功
static SysResult_t Released(void*p,const AppLifecycleIdentity_t*i){(void)p;release_checks++;CK(i->session_id==session&&i->consumer_generation==generation[i->consumer_id-1]);return drain;}
static SysResult_t Idle(void*p){(void)p;idle_checks++;return idle_result;}
static AppQualificationResourceOps_t ops={State,Start,RequestStop,ReadStop,ConsumeStop,Released,Idle};
//运输回调使用真实受控Rx；Tx WAIT_REPLY只在匹配回复被消费后完成
static SysResult_t ToCamera(void*p,const AppLifecycleMessage_t*m){uint32_t d;(void)p;return App_QualificationReceive(&core,m,&d);}
static SysResult_t ToConsumer(void*p,const AppLifecycleMessage_t*m){(void)p;reply=*m;return App_LifecycleTxAcceptReply(&tx[m->identity.consumer_id-1],m);}
//请求身份只通过编号分配和Stage绑定，不写资格表、Replay或grant
static void Begin(uint32_t c,uint32_t op,uint32_t sn,uint32_t gen)
{
 AppLifecycleMessage_t m={0};uint32_t id=0;
 CK(App_LifecycleTxReserveIds(&tx[c-1],1,&id)==SYS_OK);
 m.version=APP_LIFECYCLE_VERSION;m.kind=APP_LIFECYCLE_REQUEST;m.operation=op;
 m.source=c==1?APP_LIFECYCLE_USB:APP_LIFECYCLE_STORAGE;m.target=APP_LIFECYCLE_CAMERA;
 m.identity.session_id=sn;m.identity.request_id=id;m.identity.consumer_id=c;m.identity.consumer_generation=gen;
 request[c-1]=m;CK(App_LifecycleTxStage(&tx[c-1],&m)==SYS_OK);CK(App_LifecycleTxFlush(&tx[c-1],ToCamera,0)==SYS_OK);
}
//Process返回只代表一轮推进，Finish必须取得匹配的已固定回复
//只接收核心已经固定的重放回复，不向Tx伪造业务成功
static SysResult_t CaptureReplay(void*p,const AppLifecycleMessage_t*m){(void)p;reply=*m;return SYS_OK;}
static void Process(void){CK(App_QualificationProcess(&core,&ops,0)==SYS_OK);}
static void Finish(uint32_t c,SysResult_t expected)
{
 CK(App_QualificationFlush(&core,c,ToConsumer,0)==SYS_OK);
 CK(App_LifecycleTxGetCompleted(&tx[c-1],&reply)==SYS_OK);CK(reply.result==expected);
 CK(App_LifecycleTxConfirmConsumed(&tx[c-1],&request[c-1])==SYS_OK);
}
static AppQualificationStatus_t View(uint32_t c){AppQualificationStatus_t s;memset(&s,0,sizeof(s));CK(App_QualificationGetStatus(&core,c,&s)==SYS_OK);return s;}
static void Acquire(uint32_t c)
{
 CK(App_QualificationNextGeneration(&generation_counter[c-1],&generation[c-1])==SYS_OK);
 Begin(c,APP_LIFECYCLE_ACQUIRE,session,generation[c-1]);Process();Finish(c,SYS_OK);CK(View(c).phase==APP_QUAL_ACTIVE);
}
//公开只读视图和Tx结果确认退出仍未完成，不能用Process SYS_OK推断释放
static void StillHeld(uint32_t c)
{
 AppLifecycleMessage_t m;AppQualificationStatus_t s=View(c);
 CK(s.phase==APP_QUAL_RELEASING&&s.grant_generation==generation[c-1]);
 CK(App_LifecycleTxGetCompleted(&tx[c-1],&m)==SYS_ERR_INVALID_STATE);
 CK(stop_requests==0&&hw==APP_RESOURCE_RUNNING);
}
//每例从零初始化一次，运行中绝不重置对象
int Exit_Run(uint32_t id)
{
 uint32_t d=0,before;AppLifecycleMessage_t old,frozen;
 CK(App_QualificationInit(&core)==SYS_OK);
 CK(App_LifecycleTxInit(&tx[0],APP_LIFECYCLE_USB,APP_LIFECYCLE_CAMERA,1)==SYS_OK);
 CK(App_LifecycleTxInit(&tx[1],APP_LIFECYCLE_STORAGE,APP_LIFECYCLE_CAMERA,2)==SYS_OK);
 Begin(1,APP_LIFECYCLE_SESSION_QUERY,0,1);Process();Finish(1,SYS_OK);session=reply.identity.session_id;CK(session!=0);
 if(id==5||id==6){
  if(id==5)ops.idle=0;else idle_result=SYS_ERR_BUSY;
  Begin(1,APP_LIFECYCLE_STOP_IF_IDLE,session,1);Process();Finish(1,SYS_ERR_BUSY);
  CK(stop_requests==0&&starts==0&&hw==APP_RESOURCE_UNSTARTED);CK(!(reply.completion_flags&APP_LIFECYCLE_HARDWARE_STOPPED));return g_failure;
 }
 Acquire(1);Acquire(2);CK(starts==1);
 if(id==3){
  Begin(1,APP_LIFECYCLE_RELEASE,session+1,generation[0]);Process();Finish(1,SYS_ERR_INVALID_STATE);
  CK(View(1).phase==APP_QUAL_ACTIVE);
  Begin(1,APP_LIFECYCLE_RELEASE,session,generation[0]+1);Process();Finish(1,SYS_ERR_INVALID_STATE);
  CK(View(1).phase==APP_QUAL_ACTIVE&&release_checks==0&&stop_requests==0);return g_failure;
 }
 if(id==0)ops.released=0;
 if(id==2)drain=SYS_ERR_IO;
 if(id==4)drain=SYS_OK;
 Begin(1,APP_LIFECYCLE_RELEASE,session,generation[0]);old=request[0];Process();
 if(id==4){
  Finish(1,SYS_OK);CK(reply.completion_flags==APP_LIFECYCLE_QUALIFICATION_RELEASED);
  CK(View(1).phase==APP_QUAL_INACTIVE&&View(2).phase==APP_QUAL_ACTIVE);
  Begin(1,APP_LIFECYCLE_STOP_IF_IDLE,session,generation[0]);Process();Finish(1,SYS_ERR_BUSY);CK(stop_requests==0);return g_failure;
 }
 StillHeld(1);CK(View(2).phase==APP_QUAL_ACTIVE);
 CK(!(reply.completion_flags&APP_LIFECYCLE_QUALIFICATION_RELEASED));
 if(id==7){
  CK(App_QualificationReceive(&core,&old,&d)==SYS_OK&&d==APP_LIFECYCLE_RX_DUPLICATE_RUNNING);Process();StillHeld(1);
  drain=SYS_OK;Process();Finish(1,SYS_OK);before=release_checks;
  CK(App_QualificationReceive(&core,&old,&d)==SYS_OK&&d==APP_LIFECYCLE_RX_REPLAY);
  Process();CK(release_checks==before&&View(1).phase==APP_QUAL_INACTIVE);
  frozen=reply;CK(App_QualificationFlush(&core,1,CaptureReplay,0)==SYS_OK);CK(memcmp(&frozen,&reply,sizeof(reply))==0&&reply.completion_flags==APP_LIFECYCLE_QUALIFICATION_RELEASED);
 }
 CK(stop_requests==0);return g_failure;
}
