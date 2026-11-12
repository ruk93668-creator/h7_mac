//复用原资格专项桩，执行真实Qualification和原Rx/Replay
#include "../ConsumerQualificationC2_2A/qualification_test.c"

//验证停止完成之后的提交异常不重复硬件动作
int R2_DecisionRun(uint32_t id)
{
    uint32_t n,d;
    AppLifecycleMessage_t m;
    Reset();Query();Acquire(1U,1U);Release(1U,1U);
    Begin(1U,APP_LIFECYCLE_STOP_IF_IDLE,1U);
    (void)App_QualificationProcess(&core,&ops,NULL);
    CK(stop_valid && stops==0U && !stopped_result.complete);
    FinishStop();CK(stops==1U && stopped_result.complete);
    if(id==0U)fault_mode=1U;
    if(id==1U)fault_mode=2U;
    if(id==2U)stop_identity.session_id++;
    if(id==3U)stop_identity.consumer_generation++;
    if(id==4U)stop_identity.consumer_id=2U;
    if(id==5U)stop_identity.request_id++;
    if(id==6U)stopped_result.result=SYS_ERR_IO;
    if(id==7U)stopped_result.complete=2U;
    if(id==8U)stopped_result.hardware_stopped=2U;
    if(id==9U)stopped_result.result=SYS_ERR_BUSY;
    if(id==10U)stopped_result.result=1;
    if(id==11U){send_result=SYS_ERR_NO_RESOURCE;}
    (void)App_QualificationProcess(&core,&ops,NULL);
    n=stops;
    if(id==0U)CK(core.entries[0].decision.phase==APP_QUAL_DECIDED && !stop_valid);
    if(id>=1U && id<=10U)CK(core.entries[0].decision.phase==APP_QUAL_BLOCKED);
    if(id==11U)
    {
        CK(App_QualificationFlush(&core,1U,ToConsumer,NULL)==SYS_ERR_NO_RESOURCE);
        CK(core.rx[0].replay.completed && !stop_valid);
        m=original[0];CK(App_QualificationReceive(&core,&m,&d)==SYS_OK);
    }
    (void)App_QualificationProcess(&core,&ops,NULL);
    CK(stops==n);
    if(id==0U)CK(core.rx[0].replay.completed && core.rx[0].replay.reply.result==SYS_OK);
    if(id>=1U && id<=10U)CK(core.entries[0].decision.phase==APP_QUAL_BLOCKED);
    return g_failure;
}
