#include "../ConsumerQualificationC2_2A/qualification_test.c"
#include "app_camera_consumer.h"

//复用真实资格核心和原故障注入，仅资源完成证据由测试桩提供
static AppCameraConsumer_t clients[2];
static AppQualificationResourceOps_t test_ops;
static uint32_t request_full, reply_full;
static SysResult_t ConsumerSend(void *context, const AppLifecycleMessage_t *message)
{
    uint32_t d;
    (void)context;
    if (request_full) return SYS_ERR_NO_RESOURCE;
    return App_QualificationReceive(&core, message, &d);
}
static SysResult_t ConsumerReply(void *context, const AppLifecycleMessage_t *message)
{
    (void)context;
    if (reply_full) return SYS_ERR_NO_RESOURCE;
    return App_CameraConsumerReceive(&clients[message->identity.consumer_id - 1U], message);
}
//轮次显式区分请求运输、资格决策、资源轮末以及回复消费
static void Round(void)
{
    uint32_t i;
    for(i=0;i<2;i++) (void)App_CameraConsumerFlush(&clients[i],ConsumerSend,NULL);
    (void)App_QualificationProcess(&core,&test_ops,NULL);
    FinishStop();
    for(i=0;i<2;i++) (void)App_QualificationFlush(&core,i+1U,ConsumerReply,NULL);
    for(i=0;i<2;i++) (void)App_CameraConsumerProcess(&clients[i]);
}
static void Rounds(uint32_t n) { while(n--) Round(); }
static void StartClient(uint32_t i)
{
    CK(App_CameraConsumerBegin(&clients[i])==SYS_OK);Rounds(4);
    CK(clients[i].phase==APP_CONSUMER_GRANTED && clients[i].granted==1U);
}
int Integration_Run(uint32_t id)
{
    uint32_t i;
    AppLifecycleMessage_t bad;
    Reset();memset(clients,0,sizeof(clients));test_ops=ops;
    request_full=reply_full=0U;
    CK(App_CameraConsumerInit(&clients[0],APP_LIFECYCLE_USB,1U)==SYS_OK);
    CK(App_CameraConsumerInit(&clients[1],APP_LIFECYCLE_STORAGE,2U)==SYS_OK);
    switch(id)
    {
    case 0: StartClient(0);CK(starts==1 && core.entries[0].phase==APP_QUAL_ACTIVE);break;
    case 1: StartClient(1);CK(starts==1 && core.entries[1].phase==APP_QUAL_ACTIVE);break;
    case 2: StartClient(0);StartClient(1);CK(starts==1 && clients[0].session==clients[1].session);break;
    case 3: StartClient(0);CK(App_CameraConsumerBegin(&clients[0])==SYS_ERR_BUSY);break;
    case 4:
        StartClient(0);test_ops.released=NULL;test_ops.idle=NULL;
        CK(App_CameraConsumerEnd(&clients[0],SYS_OK,SYS_OK)==SYS_OK);Rounds(20);
        CK(clients[0].phase==APP_CONSUMER_RELEASE && clients[0].granted==1);
        CK(core.entries[0].phase==APP_QUAL_RELEASING && core.rx[0].replay.completed==0 && stops==0);break;
    case 5: case 6:
        StartClient(0);StartClient(1);i=id==5?0:1;drain[i]=SYS_OK;
        CK(App_CameraConsumerEnd(&clients[i],SYS_OK,SYS_OK)==SYS_OK);Rounds(6);
        CK(clients[i].phase==APP_CONSUMER_IDLE && clients[i].granted==0);
        CK(clients[1-i].granted==1 && hw_state==APP_RESOURCE_RUNNING && stops==0);break;
    case 7:
        CK(App_CameraConsumerBegin(&clients[0])==SYS_OK);Round();
        bad=clients[0].request;bad.identity.consumer_generation++;
        CK(App_LifecycleMakeReply(&bad,core.session_id,SYS_OK,&sent)==SYS_OK);
        CK(App_CameraConsumerReceive(&clients[0],&sent)!=SYS_OK);Rounds(4);
        CK(clients[0].granted==1 && starts==1);break;
    case 8:
        request_full=1;CK(App_CameraConsumerBegin(&clients[0])==SYS_OK);i=clients[0].request.identity.request_id;
        Rounds(8);CK(clients[0].request.identity.request_id==i && starts==0);
        request_full=0;Rounds(4);CK(clients[0].granted==1 && starts==1);break;
    case 9:
        reply_full=1;CK(App_CameraConsumerBegin(&clients[0])==SYS_OK);Rounds(8);
        CK(clients[0].phase==APP_CONSUMER_QUERY && starts==0 && core.rx[0].final_reply_enqueued==0);
        reply_full=0;Rounds(6);CK(clients[0].granted==1 && starts==1);break;
    case 10:
        CK(App_CameraConsumerBegin(&clients[0])==SYS_OK);Round();fault_mode=1;
        Round();CK(starts==1 && core.entries[0].decision.phase==APP_QUAL_DECIDED);
        Rounds(5);CK(starts==1 && clients[0].granted==1);break;
    case 11:
        start_result=SYS_ERR_IO;CK(App_CameraConsumerBegin(&clients[0])==SYS_OK);Rounds(4);
        CK(clients[0].granted==0 && clients[0].phase==APP_CONSUMER_FAILED && clients[0].result==SYS_ERR_IO);break;
    case 12:
        StartClient(0);drain[0]=SYS_OK;stop_result=SYS_ERR_IO;
        CK(App_CameraConsumerEnd(&clients[0],SYS_OK,SYS_OK)==SYS_OK);Rounds(6);
        CK(clients[0].granted==0 && clients[0].result==SYS_ERR_IO && stops==1);break;
    case 13:
        drain[0]=SYS_OK;
        for(i=0;i<1000;i++)
        {
            StartClient(0);CK(App_CameraConsumerEnd(&clients[0],SYS_OK,SYS_OK)==SYS_OK);Rounds(6);
            CK(clients[0].phase==APP_CONSUMER_IDLE && clients[0].granted==0);
        }
        CK(starts==1000 && stops==1000 && core.rx[0].highest_retired_request_id>3900);break;
    case 14:
        clients[0].next_generation=UINT32_MAX;
        CK(App_CameraConsumerBegin(&clients[0])==SYS_ERR_NO_RESOURCE && clients[0].granted==0);break;
    case 15:
        StartClient(0);CK(App_CameraConsumerEnd(&clients[0],SYS_OK,SYS_OK)==SYS_OK);Rounds(6);
        CK(App_CameraConsumerBegin(&clients[0])==SYS_ERR_BUSY && clients[0].granted==1);break;
    case 16:
        StartClient(0);drain[0]=SYS_OK;stop_result=SYS_ERR_BUSY;
        CK(App_CameraConsumerEnd(&clients[0],SYS_OK,SYS_OK)==SYS_OK);Rounds(6);
        CK(clients[0].phase==APP_CONSUMER_STOP_IDLE && clients[0].granted==0);
        CK(App_CameraConsumerBegin(&clients[0])==SYS_ERR_BUSY);break;
    default: CK(0);break;
    }
    return g_failure;
}
