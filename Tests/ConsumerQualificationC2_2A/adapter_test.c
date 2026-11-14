//复用原B1依赖桩，编译真实App_Camera、CameraTask及本次资格核心
#include "../CameraModuleB1/behavior_test.c"
#include "app_camera_qualification_internal.h"

volatile uint32_t g_abi[8];
static SysResult_t Evidence(void *context) { (void)context; return SYS_OK; }
static SysResult_t ReleasedEvidence(void *context, const AppLifecycleIdentity_t *grant)
{ (void)context; return grant->request_id != 0U ? SYS_OK : SYS_ERR_PROTOCOL; }
static SysResult_t DropReply(void *context, const AppLifecycleMessage_t *reply)
{ (void)context; return App_LifecycleValidate(reply) ? SYS_OK : SYS_ERR_PROTOCOL; }
static AppLifecycleMessage_t Make(uint32_t c, uint32_t op, uint32_t id)
{
    AppLifecycleMessage_t r = {0};
    r.version=APP_LIFECYCLE_VERSION; r.kind=APP_LIFECYCLE_REQUEST; r.operation=op;
    r.source=c==1U?APP_LIFECYCLE_USB:APP_LIFECYCLE_STORAGE; r.target=APP_LIFECYCLE_CAMERA;
    r.identity.consumer_id=c; r.identity.consumer_generation=1U; r.identity.request_id=id;
    r.identity.session_id=op==APP_LIFECYCLE_SESSION_QUERY?0U:s_camera.qualifications.session_id;
    return r;
}
static SysResult_t Perform(uint32_t c, uint32_t op, uint32_t id)
{
    uint32_t disposition;
    AppLifecycleMessage_t r=Make(c,op,id);
    SysResult_t result=App_CameraQualificationReceive(&r,&disposition);
    if(result!=SYS_OK)return result;
    result=App_CameraQualificationProcess();
    if(result!=SYS_OK)return result;
    return App_CameraQualificationFlush(c,DropReply,NULL);
}
//旧断言保留，资源完成改由真实Camera轮末推进
static SysResult_t ProcessRound(const AppQualificationResourceOps_t *ops)
{
    SysResult_t result;
    App_CameraBeginRound();
    result=App_QualificationProcess(&s_camera.qualifications,ops,NULL);
    App_CameraFinishRound();
    if (result!=SYS_OK) return result;
    return App_QualificationProcess(&s_camera.qualifications,ops,NULL);
}
#define VERIFY(x) do { if (!(x)) { g_failure=__LINE__; return (int)g_failure; } } while(0)

//所有硬件动作仍为B1桩，只验证原资源路径调用顺序与资格适配边界
int C2A_AdapterRun(uint32_t id)
{
    uint32_t i, device_starts=0U, disposition;
    AppLifecycleMessage_t r;
    AppQualificationStatus_t view;
    AppQualificationResourceOps_t proven;
    sensor.base.state=DEVICE_STATE_READY;
    g_abi[0]=sizeof(s_camera); g_abi[1]=sizeof(s_camera.qualifications);
    g_abi[2]=sizeof(AppQualificationDecision_t);g_abi[3]=sizeof(AppLifecycleMessage_t);
    g_abi[4]=sizeof(AppMessage_t);g_abi[5]=sizeof(CameraFrame_t);
    g_abi[6]=sizeof(s_camera_stack);g_abi[7]=CAMERA_TASK_PRIORITY;
    if(id==0U){owner=false;VERIFY(App_CameraQualificationPrepare()==SYS_ERR_INVALID_STATE);return 0;}
    if(id==12U)
    {
        VERIFY(App_CameraInit()==SYS_OK);
        VERIFY(App_CameraQualificationPrepare()==SYS_ERR_INVALID_STATE);return 0;
    }
    if(id==13U)
    {
        s_camera.configured=true;
        VERIFY(App_CameraQualificationPrepare()==SYS_ERR_INVALID_STATE);return 0;
    }
    VERIFY(App_CameraQualificationPrepare()==SYS_OK);
    if(id==1U){VERIFY(App_CameraQualificationPrepare()==SYS_ERR_ALREADY_EXISTS);return 0;}
    VERIFY(Perform(1U,APP_LIFECYCLE_SESSION_QUERY,1U)==SYS_OK);
    if(id==2U){VERIFY(g_trace_count==0U);return 0;}
    if(id==3U)
    {
        owner=false;VERIFY(App_CameraQualificationGetStatus(1U,&view)==SYS_ERR_INVALID_STATE);
        VERIFY(App_CameraQualificationProcess()==SYS_ERR_INVALID_STATE);return 0;
    }
    VERIFY(Perform(1U,APP_LIFECYCLE_ACQUIRE,2U)==SYS_OK);
    if(id==4U)
    {
        VERIFY(s_camera.running && s_camera.device==&sensor.base && sensor.base.state==DEVICE_STATE_RUNNING);
        VERIFY(s_camera.qualifications.entries[0].phase==APP_QUAL_ACTIVE);return 0;
    }
    if(id==5U)
    {
        VERIFY(Perform(2U,APP_LIFECYCLE_ACQUIRE,1U)==SYS_OK);
        for(i=0U;i<g_trace_count;i++)if(g_trace[i][0]==22U)device_starts++;
        VERIFY(device_starts==1U);return 0;
    }
    if(id==6U)
    {
        r=Make(1U,APP_LIFECYCLE_RELEASE,3U);r.completion_flags=APP_LIFECYCLE_CONSUMER_FLAGS;
        VERIFY(App_CameraQualificationReceive(&r,&disposition)==SYS_OK);
        VERIFY(App_CameraQualificationProcess()==SYS_OK);
        VERIFY(s_camera.qualifications.entries[0].phase==APP_QUAL_RELEASING && s_camera.running);
        VERIFY(s_camera.qualifications.rx[0].replay.completed==0U);return 0;
    }
    if(id==7U){VERIFY(App_CameraQualificationPrepare()==SYS_ERR_INVALID_STATE);return 0;}
    if(id==8U)
    {
        process_result=SYS_ERR_IO;App_CameraProcess();
        VERIFY(s_camera.stopping && s_camera.qualifications.entries[0].phase==APP_QUAL_ACTIVE);
        VERIFY(App_CameraResourceState(NULL)==APP_RESOURCE_FAULT_DRAINING);return 0;
    }

    //以下只在独立测试绑定明确证据，生产s_qualification_ops仍保持空证明
    proven=s_qualification_ops;proven.released=ReleasedEvidence;proven.idle=Evidence;
    r=Make(1U,APP_LIFECYCLE_RELEASE,3U);
    VERIFY(App_CameraQualificationReceive(&r,&disposition)==SYS_OK);
    VERIFY(ProcessRound(&proven)==SYS_OK);
    VERIFY(App_CameraQualificationFlush(1U,DropReply,NULL)==SYS_OK);
    r=Make(1U,APP_LIFECYCLE_STOP_IF_IDLE,4U);
    VERIFY(App_CameraQualificationReceive(&r,&disposition)==SYS_OK);
    if(id==9U)bridge_idle=false;
    if(id==10U)stop_error_rounds=1U;
    VERIFY(ProcessRound(&proven)==SYS_OK);
    if(id==9U)
    {
        VERIFY(s_camera.stopping && s_camera.qualifications.rx[0].replay.completed==0U);
        bridge_idle=true;
        VERIFY(ProcessRound(&proven)==SYS_OK);
    }
    if(id==10U)
    {
        VERIFY(s_camera.fault==SYS_ERR_IO && s_camera.qualifications.rx[0].replay.reply.result==SYS_ERR_IO);
        VERIFY(s_camera.qualifications.session_closed==0U);return 0;
    }
    VERIFY(s_camera.resource_stop_confirmed && !s_camera.running && !s_camera.stopping);
    VERIFY(s_camera.qualifications.rx[0].replay.reply.completion_flags==APP_LIFECYCLE_HARDWARE_STOPPED);
    return 0;
}
