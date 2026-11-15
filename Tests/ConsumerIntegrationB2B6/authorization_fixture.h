//仅测试运输使用静态队列；事务、Replay和资格决策全部调用真实生产实现
#include "app_camera_consumer.h"
//记录每种操作、真实回复及资源桩调用，供Python保存完整证据
volatile uint32_t g_b2b6_proof[10];
//固定三条测试运输通道，不改变生产队列容量
static AppLifecycleMessage_t b6_messages[3][8];
static uint32_t b6_head[3],b6_count[3];
//独立测试资源桩只描述硬件是否运行，不保存或修改消费者资格
#ifndef B2B6_REAL_CAMERA
static AppCameraQualification_t b6_core;
static bool b6_resource_running;
#endif
static bool b6_initialized;
//初始化一个测试接收端，不能重置已经运行的Replay
static SysResult_t B6_Init(void)
{
 SysResult_t r;
 if(b6_initialized)return SYS_OK;
#ifdef B2B6_REAL_CAMERA
 Owner(RUNTIME_TARGET_CAMERA);r=App_CameraQualificationPrepare();Owner(RUNTIME_TARGET_STORAGE);
#else
 r=App_QualificationInit(&b6_core);
#endif
 if(r==SYS_OK)b6_initialized=true;
 return r;
}
#ifndef B2B6_REAL_CAMERA
//仅模拟资源启动，资格字段必须由真实Qualification写入
static uint32_t B6_State(void *x){(void)x;return b6_resource_running?APP_RESOURCE_RUNNING:APP_RESOURCE_UNSTARTED;}
//记录资源被启动一次，不提供任何释放证明
static SysResult_t B6_Start(void *x){(void)x;g_b2b6_proof[4]++;b6_resource_running=true;return SYS_OK;}
//released、idle和stop回调故意不绑定，RELEASE不能伪造成功
//停止接口明确返回未实现，不构造释放或硬件完成证据
static SysResult_t B6_Stop(void *x,const AppLifecycleIdentity_t *i){(void)x;(void)i;return SYS_ERR_NOT_SUPPORTED;}
//查询不存在的停止结果只能失败
static SysResult_t B6_ReadStop(void *x,const AppLifecycleIdentity_t *i,AppQualificationStopResult_t *r){(void)x;(void)i;(void)r;return SYS_ERR_NOT_SUPPORTED;}
static const AppQualificationResourceOps_t b6_ops={B6_State,B6_Start,B6_Stop,B6_ReadStop,B6_Stop,0,0};
#endif
//校验格式和方向后才接受入队，入队不代表业务完成
SysResult_t App_LifecycleMessagesSend(AppLifecycleChannel_t c,const AppLifecycleMessage_t *m)
{
 uint32_t i=(uint32_t)c;
 if(!m||!App_LifecycleValidate(m))return SYS_ERR_INVALID_ARG;
 if(i>=3U)return SYS_ERR_INVALID_ARG;
 if((i==0U&&m->target!=APP_LIFECYCLE_CAMERA)||(i==1U&&m->target!=APP_LIFECYCLE_USB)||(i==2U&&m->target!=APP_LIFECYCLE_STORAGE))return SYS_ERR_INVALID_STATE;
 if(b6_count[i]==8U)return SYS_ERR_NO_RESOURCE;
 b6_messages[i][(b6_head[i]+b6_count[i])%8U]=*m;b6_count[i]++;
 return SYS_OK;
}
//逐条复制出队，不把任务通知当成可靠消息
bool App_LifecycleMessagesReceive(AppLifecycleChannel_t c,AppLifecycleMessage_t *m)
{
 uint32_t i=(uint32_t)c;
 if(i>=3U||!m||!b6_count[i])return false;
 *m=b6_messages[i][b6_head[i]];b6_head[i]=(b6_head[i]+1U)%8U;b6_count[i]--;return true;
}
//Consumer的原Flush回调，不能直接制造ACK
static SysResult_t B6_Send(void *x,const AppLifecycleMessage_t *m)
{(void)x;return App_LifecycleMessagesSend(APP_LIFECYCLE_CHANNEL_CAMERA,m);}
//只有Qualification经RxComplete冻结的回复才能进入回复队列
static SysResult_t B6_Reply(void *x,const AppLifecycleMessage_t *m)
{(void)x;g_b2b6_proof[3]++;return App_LifecycleMessagesSend(m->target==APP_LIFECYCLE_USB?APP_LIFECYCLE_CHANNEL_USB:APP_LIFECYCLE_CHANNEL_STORAGE,m);}
//运行Camera一轮协议；真实资源释放接口始终缺席，不涉及C2.2C
static void B6_Camera(void)
{
 AppLifecycleMessage_t m;uint32_t d;SysResult_t r;
#ifdef B2B6_REAL_CAMERA
 Owner(RUNTIME_TARGET_CAMERA);
#endif
 while(App_LifecycleMessagesReceive(APP_LIFECYCLE_CHANNEL_CAMERA,&m))
 {
  if(m.operation==APP_LIFECYCLE_SESSION_QUERY)g_b2b6_proof[0]++;
  if(m.operation==APP_LIFECYCLE_ACQUIRE)g_b2b6_proof[1]++;
  if(m.operation==APP_LIFECYCLE_RELEASE)g_b2b6_proof[2]++;
#ifdef B2B6_REAL_CAMERA
  r=App_CameraQualificationReceive(&m,&d);
#else
  r=App_QualificationReceive(&b6_core,&m,&d);
#endif
  if(r!=SYS_OK&&r!=SYS_ERR_BUSY){g_b2b6_proof[5]++;g_b2b6_proof[8]=(uint32_t)r;}
 }
#ifdef B2B6_REAL_CAMERA
 (void)App_CameraQualificationProcess();
 (void)App_CameraQualificationFlush(APP_CONSUMER_STORAGE,B6_Reply,0);
 Owner(RUNTIME_TARGET_STORAGE);
#else
 r=App_QualificationProcess(&b6_core,&b6_ops,0);
 if(r!=SYS_OK&&r!=SYS_ERR_BUSY){g_b2b6_proof[5]++;g_b2b6_proof[8]=(uint32_t)r;}
 (void)App_QualificationFlush(&b6_core,APP_CONSUMER_USB,B6_Reply,0);
 (void)App_QualificationFlush(&b6_core,APP_CONSUMER_STORAGE,B6_Reply,0);
#endif
}
#ifdef B2B6_USB
//补齐异步前置阶段，不修改phase/granted，也不隐藏产生的诊断轨迹
static void B2B6_PumpUsb(void)
{
 AppCameraConsumerStatus_t s;AppLifecycleMessage_t m;uint32_t i;
 App_USBGetLifecycleStatus(&s);
 if(s.phase!=APP_CONSUMER_QUERY&&s.phase!=APP_CONSUMER_ACQUIRE)return;
 if(B6_Init()!=SYS_OK){g_b2b6_proof[5]++;return;}
 for(i=0;i<8;i++)
 {
  (void)App_USBFlushLifecycle(B6_Send,0);B6_Camera();
  while(App_LifecycleMessagesReceive(APP_LIFECYCLE_CHANNEL_USB,&m))(void)App_USBReceiveLifecycle(&m);
  App_USBProcess();g_b2b6_proof[6]++;
  App_USBGetLifecycleStatus(&s);
  if(s.phase!=APP_CONSUMER_QUERY&&s.phase!=APP_CONSUMER_ACQUIRE)break;
 }
 if(s.phase==APP_CONSUMER_GRANTED)g_b2b6_proof[7]++;
}
#else
//文件启动前驱动真实QUERY/ACQUIRE，不跳过文件或帧归还断言
static void B2B6_PumpStorage(void)
{
 AppCameraConsumerStatus_t s;AppLifecycleMessage_t m;uint32_t i;
 App_StorageGetLifecycleStatus(&s);
 if(s.phase!=APP_CONSUMER_QUERY&&s.phase!=APP_CONSUMER_ACQUIRE)return;
 if(B6_Init()!=SYS_OK){g_b2b6_proof[5]++;return;}
 for(i=0;i<8;i++)
 {
  (void)App_StorageFlushLifecycle(B6_Send,0);B6_Camera();
  while(App_LifecycleMessagesReceive(APP_LIFECYCLE_CHANNEL_STORAGE,&m))(void)App_StorageReceiveLifecycle(&m);
  App_StorageProcess();g_b2b6_proof[6]++;
  App_StorageGetLifecycleStatus(&s);
  if(s.phase!=APP_CONSUMER_QUERY&&s.phase!=APP_CONSUMER_ACQUIRE)break;
 }
 if(s.phase==APP_CONSUMER_GRANTED)g_b2b6_proof[7]++;
}
#endif
