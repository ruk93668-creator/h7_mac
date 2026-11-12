#include "app_camera_qualification.h"
#include <string.h>

//包装真实Complete进行错误注入，不复制或放宽Replay实现
static SysResult_t Test_Complete(AppLifecycleRxContext_t *rx, const AppLifecycleMessage_t *reply);
#define App_LifecycleRxComplete Test_Complete
#include "../../CM7/Application/app_camera_qualification.c"
#undef App_LifecycleRxComplete

volatile uint32_t g_failure;
static AppCameraQualification_t core, before;
static AppLifecycleTxContext_t tx[2];
static AppLifecycleMessage_t sent, original[2];
static uint32_t hw_state, starts, stops, releases[2], checks, fault_mode, send_count;
static uint32_t generation[2], reentry;
static SysResult_t start_result, stop_result, drain[2], idle_result, send_result;
static const AppQualificationResourceOps_t ops;
#define CK(x) do { if (!(x) && g_failure == 0U) g_failure = __LINE__; } while (0)

//只有测试故障入口可以模拟库异常，正常路径始终调用原实现
static SysResult_t Test_Complete(AppLifecycleRxContext_t *rx, const AppLifecycleMessage_t *reply)
{
    uint32_t mode = fault_mode;
    SysResult_t result;
    fault_mode = 0U;
    if (mode == 1U)
    {
        //模拟另一调用仍占用入口；调用真实RxComplete并核对它未写Replay
        AppLifecycleReplay_t old = rx->replay;
        uint32_t busy = rx->api_busy;
        rx->api_busy = 1U;
        result = App_LifecycleRxComplete(rx, reply);
        CK(result == SYS_ERR_BUSY && memcmp(&old, &rx->replay, sizeof(old)) == 0);
        rx->api_busy = busy;//仅测试模拟原占用调用退出，生产代码禁止直接修改
        return result;
    }
    if (mode == 2U) return SYS_ERR_INVALID_ARG;
    result = App_LifecycleRxComplete(rx, reply);
    if (mode == 3U) return SYS_ERR_ALREADY_EXISTS;
    if (mode == 4U) rx->replay.reply.result = SYS_ERR_IO;
    if (mode == 5U) memset(&rx->reply_pending, 0, sizeof(rx->reply_pending));
    if (mode == 6U) rx->reply_pending.message.identity.consumer_generation++;
    if (mode == 7U) return SYS_ERR_IO;
    return result;
}

//资源桩只代表资格决策证据，不宣称硬件归还或停止通过
static uint32_t State(void *context) { (void)context; return hw_state; }
static SysResult_t Start(void *context)
{
    AppQualificationStatus_t view;
    (void)context; starts++;
    if (reentry != 0U)
    {
        CK(App_QualificationProcess(&core, &ops, NULL) == SYS_ERR_BUSY);
        CK(App_QualificationGetStatus(&core, 1U, &view) == SYS_ERR_BUSY);
    }
    hw_state = start_result == SYS_OK ? APP_RESOURCE_RUNNING : APP_RESOURCE_FAULT_DRAINING;
    return start_result;
}
//异步资源桩保存身份，实际停止由测试调度边界推进
static AppLifecycleIdentity_t stop_identity;
static AppQualificationStopResult_t stopped_result;
static uint32_t stop_valid;
static SysResult_t RequestStop(void *context, const AppLifecycleIdentity_t *identity)
{
    (void)context;
    if (stop_valid) return memcmp(&stop_identity,identity,sizeof(*identity)) == 0 ? SYS_OK : SYS_ERR_BUSY;
    stop_identity=*identity;stop_valid=1U;memset(&stopped_result,0,sizeof(stopped_result));return SYS_OK;
}
static SysResult_t ReadStop(void *context, const AppLifecycleIdentity_t *identity, AppQualificationStopResult_t *result)
{
    (void)context;
    if (!stop_valid) return SYS_ERR_NOT_FOUND;
    if (memcmp(&stop_identity,identity,sizeof(*identity)) != 0) return SYS_ERR_PROTOCOL;
    *result=stopped_result;return SYS_OK;
}
static SysResult_t ConsumeStop(void *context, const AppLifecycleIdentity_t *identity)
{
    (void)context;
    if (!stop_valid || !stopped_result.complete || memcmp(&stop_identity,identity,sizeof(*identity)) != 0) return SYS_ERR_PROTOCOL;
    stop_valid=0U;return SYS_OK;
}
static void FinishStop(void)
{
    if (!stop_valid || stopped_result.complete) return;
    stops++;
    hw_state = stop_result == SYS_OK ? APP_RESOURCE_STOPPED_CONFIRMED :
        stop_result == SYS_ERR_BUSY ? APP_RESOURCE_STOPPING : APP_RESOURCE_FAULT_DRAINING;
    if (stop_result != SYS_ERR_BUSY)
    {
        stopped_result.complete=1U;stopped_result.result=stop_result;
        stopped_result.hardware_stopped=stop_result==SYS_OK;
    }
}
static SysResult_t Released(void *context, const AppLifecycleIdentity_t *grant)
{
    (void)context; releases[grant->consumer_id - 1U]++;
    CK(grant->session_id == core.session_id);
    CK(grant->request_id == core.entries[grant->consumer_id - 1U].grant_request);
    return drain[grant->consumer_id - 1U];
}
static SysResult_t Idle(void *context) { (void)context; checks++; return idle_result; }
static const AppQualificationResourceOps_t ops = { State, Start, RequestStop, ReadStop, ConsumeStop, Released, Idle };

//真实Tx在入队完成后保留WAIT_REPLY，回复消费与入队分开
static SysResult_t ToCamera(void *context, const AppLifecycleMessage_t *message)
{
    uint32_t disposition = 0U;
    (void)context;
    return App_QualificationReceive(&core, message, &disposition);
}
static SysResult_t ToConsumer(void *context, const AppLifecycleMessage_t *reply)
{
    (void)context; send_count++;
    if (send_result != SYS_OK) return send_result;
    sent = *reply;
    return App_LifecycleTxAcceptReply(&tx[reply->identity.consumer_id - 1U], reply);
}
static SysResult_t Discard(void *context, const AppLifecycleMessage_t *reply)
{
    (void)context; sent = *reply; send_count++;
    return send_result;
}
static void Reset(void)
{
    memset(&core, 0, sizeof(core)); memset(tx, 0, sizeof(tx));
    memset(generation, 0, sizeof(generation));
    stop_valid = 0U; g_failure = 0U; starts = stops = checks = fault_mode = send_count = reentry = 0U;
    releases[0] = releases[1] = 0U;
    hw_state = APP_RESOURCE_UNSTARTED;
    start_result = stop_result = idle_result = send_result = SYS_OK;
    drain[0] = drain[1] = SYS_ERR_BUSY;
    CK(App_QualificationInit(&core) == SYS_OK);
    CK(App_LifecycleTxInit(&tx[0], APP_LIFECYCLE_USB, APP_LIFECYCLE_CAMERA, 1U) == SYS_OK);
    CK(App_LifecycleTxInit(&tx[1], APP_LIFECYCLE_STORAGE, APP_LIFECYCLE_CAMERA, 2U) == SYS_OK);
}
static AppLifecycleMessage_t Request(uint32_t consumer, uint32_t operation, uint32_t gen)
{
    AppLifecycleMessage_t m = {0}; uint32_t id = 0U;
    CK(App_LifecycleTxReserveIds(&tx[consumer-1U], 1U, &id) == SYS_OK);
    m.version = APP_LIFECYCLE_VERSION; m.kind = APP_LIFECYCLE_REQUEST;
    m.operation = operation; m.source = consumer == 1U ? APP_LIFECYCLE_USB : APP_LIFECYCLE_STORAGE;
    m.target = APP_LIFECYCLE_CAMERA; m.identity.session_id = operation == APP_LIFECYCLE_SESSION_QUERY ? 0U : core.session_id;
    m.identity.consumer_id = consumer; m.identity.request_id = id; m.identity.consumer_generation = gen;
    return m;
}
static void BeginMessage(AppLifecycleMessage_t m)
{
    uint32_t index = m.identity.consumer_id - 1U;
    original[index] = m;
    CK(App_LifecycleTxStage(&tx[index], &m) == SYS_OK);
    CK(App_LifecycleTxFlush(&tx[index], ToCamera, NULL) == SYS_OK);
}
static void Begin(uint32_t consumer, uint32_t op, uint32_t gen) { BeginMessage(Request(consumer, op, gen)); }
//保留原断言的完成检查点，显式模拟任务轮末后再读取停止结果
static void Process(void)
{
    (void)App_QualificationProcess(&core, &ops, NULL);
    if (stop_valid && !stopped_result.complete)
    {
        FinishStop();
        if (stopped_result.complete) (void)App_QualificationProcess(&core, &ops, NULL);
    }
}
static void Finish(uint32_t consumer, SysResult_t expected)
{
    AppLifecycleMessage_t reply;
    CK(App_QualificationFlush(&core, consumer, ToConsumer, NULL) == SYS_OK);
    CK(App_LifecycleTxGetCompleted(&tx[consumer-1U], &reply) == SYS_OK);
    CK(reply.result == expected);
    CK(App_LifecycleTxConfirmConsumed(&tx[consumer-1U], &original[consumer-1U]) == SYS_OK);
}
static void Exchange(uint32_t c, uint32_t op, uint32_t gen, SysResult_t expected)
{ Begin(c,op,gen); Process(); Finish(c,expected); }
static void Query(void) { Exchange(1U,APP_LIFECYCLE_SESSION_QUERY,1U,SYS_OK); }
static void Acquire(uint32_t c, uint32_t gen) { Exchange(c,APP_LIFECYCLE_ACQUIRE,gen,SYS_OK); }
static void Release(uint32_t c, uint32_t gen)
{ drain[c-1U]=SYS_OK; Exchange(c,APP_LIFECYCLE_RELEASE,gen,SYS_OK); }

//Q01至Q44逐项验证真实资格核心及受控Tx/Rx；依赖证据的场景明确使用桩
int C2A_Run(uint32_t id)
{
    uint32_t d=0U, n=0U, i;
    AppLifecycleMessage_t m, cached;
    AppQualificationStatus_t view;
    AppQualificationResourceOps_t missing = ops;
    Reset();
    switch (id)
    {
    case 0: Query(); CK(core.session_id==1U && starts==0U && stops==0U && core.entries[0].phase==0U); break;
    case 1:
        Query(); cached=sent; m=original[0];
        CK(App_QualificationReceive(&core,&m,&d)==SYS_OK && d==APP_LIFECYCLE_RX_REPLAY);
        Process(); CK(App_QualificationFlush(&core,1U,Discard,NULL)==SYS_OK && memcmp(&sent,&cached,sizeof(sent))==0);
        Exchange(1,APP_LIFECYCLE_SESSION_QUERY,1,SYS_OK);
        CK(App_QualificationReceive(&core,&m,&d)==SYS_ERR_PROTOCOL && d==APP_LIFECYCLE_RX_STALE); break;
    case 2: Query(); Acquire(1,1); CK(starts==1U && core.entries[0].phase==APP_QUAL_ACTIVE); break;
    case 3: Query(); Acquire(1,1); Acquire(2,1); CK(starts==1U && core.entries[1].phase==APP_QUAL_ACTIVE); break;
    case 4:
        Query(); Exchange(2,APP_LIFECYCLE_SESSION_QUERY,1,SYS_OK); Acquire(1,1); Acquire(2,1);
        CK(core.entries[0].grant_request==core.entries[1].grant_request && starts==1U); break;
    case 5:
        Query(); Acquire(1,1); m=original[0];
        CK(App_QualificationReceive(&core,&m,&d)==SYS_OK && d==APP_LIFECYCLE_RX_REPLAY);
        Process(); CK(starts==1U && core.entries[0].highest_seen_generation==1U); break;
    case 6: Query(); Acquire(1,1); Exchange(1,APP_LIFECYCLE_ACQUIRE,2,SYS_ERR_BUSY); CK(starts==1U); break;
    case 7:
        Query(); start_result=SYS_ERR_IO; Exchange(1,APP_LIFECYCLE_ACQUIRE,1,SYS_ERR_IO);
        Exchange(1,APP_LIFECYCLE_ACQUIRE,1,SYS_ERR_INVALID_STATE); CK(starts==1U); break;
    case 8:
        Query(); start_result=SYS_ERR_IO; Exchange(1,APP_LIFECYCLE_ACQUIRE,1,SYS_ERR_IO);
        start_result=SYS_OK; Exchange(1,APP_LIFECYCLE_ACQUIRE,2,SYS_ERR_BUSY);
        hw_state=APP_RESOURCE_STOPPED_CONFIRMED; Acquire(1,3); CK(starts==2U); break;
    case 9:
        CK(App_QualificationNextGeneration(&n,&d)==SYS_OK && n==1U && d==1U);
        n=UINT32_MAX; d=17U; CK(App_QualificationNextGeneration(&n,&d)==SYS_ERR_NO_RESOURCE && d==17U);
        Query(); Acquire(1,UINT32_MAX); Release(1,UINT32_MAX);
        Exchange(1,APP_LIFECYCLE_ACQUIRE,UINT32_MAX,SYS_ERR_INVALID_STATE); break;
    case 10: Query(); Acquire(1,1); Exchange(1,APP_LIFECYCLE_ACQUIRE,99,SYS_ERR_BUSY); Release(1,1); CK(core.entries[0].phase==0U); break;
    case 11:
        Query(); m=Request(1,APP_LIFECYCLE_ACQUIRE,1); m.identity.session_id++;
        BeginMessage(m); Process(); Finish(1,SYS_ERR_INVALID_STATE); CK(starts==0U && core.entries[0].highest_seen_generation==0U); break;
    case 12:
        m=Request(1,APP_LIFECYCLE_SESSION_QUERY,1);
        CK(App_LifecycleTxStage(&tx[0],&m)==SYS_OK); CK(App_LifecycleTxCancelReady(&tx[0],&m)==SYS_OK);
        CK(App_LifecycleTxStage(&tx[0],&m)==SYS_ERR_PROTOCOL);
        Begin(1,APP_LIFECYCLE_SESSION_QUERY,2); CK(App_LifecycleTxCancelReady(&tx[0],&original[0])==SYS_ERR_INVALID_STATE); break;
    case 13:
        Query(); Acquire(1,1); m=Request(1,APP_LIFECYCLE_RELEASE,1); m.completion_flags=APP_LIFECYCLE_CONSUMER_FLAGS;
        BeginMessage(m); Process(); CK(core.entries[0].phase==APP_QUAL_RELEASING && core.rx[0].replay.completed==0U); break;
    case 14:
        Query(); Acquire(1,1); Release(1,1); CK(core.entries[0].phase==0U && sent.completion_flags==APP_LIFECYCLE_QUALIFICATION_RELEASED && stops==0U); break;
    case 15:
        Query(); Acquire(1,1); Begin(1,APP_LIFECYCLE_RELEASE,1); drain[0]=SYS_ERR_IO;
        for(i=0;i<100;i++)Process(); CK(core.entries[0].phase==APP_QUAL_RELEASING && core.rx[0].replay.completed==0U); break;
    case 16:
        Query(); Acquire(1,1); Release(1,1); m=original[0]; n=releases[0];
        CK(App_QualificationReceive(&core,&m,&d)==SYS_OK); Process(); CK(releases[0]==n); break;
    case 17: Query(); Acquire(1,1); Release(1,1); Exchange(1,APP_LIFECYCLE_RELEASE,1,SYS_ERR_INVALID_STATE); break;
    case 18: Query(); Acquire(1,1); Acquire(2,1); Release(1,1); Exchange(1,APP_LIFECYCLE_STOP_IF_IDLE,1,SYS_ERR_BUSY); CK(stops==0U && core.entries[1].phase==APP_QUAL_ACTIVE); break;
    case 19:
        Query(); Acquire(1,1); Acquire(2,1); drain[1]=SYS_OK;
        m=Request(2,APP_LIFECYCLE_RELEASE,1); m.business_result=SYS_ERR_IO; m.cleanup_result=SYS_ERR_IO;
        BeginMessage(m); Process(); Finish(2,SYS_OK); CK(sent.business_result==SYS_ERR_IO && sent.cleanup_result==SYS_ERR_IO && sent.completion_flags==APP_LIFECYCLE_QUALIFICATION_RELEASED && core.entries[0].phase==APP_QUAL_ACTIVE); break;
    case 20:
        Query(); Acquire(1,1); Acquire(2,1); Begin(1,APP_LIFECYCLE_RELEASE,1); Begin(2,APP_LIFECYCLE_RELEASE,1);
        Process(); CK(core.entries[0].phase==3U && core.entries[1].phase==3U && stops==0U);
        drain[0]=drain[1]=SYS_OK; Process(); Finish(1,SYS_OK); Finish(2,SYS_OK); Exchange(1,APP_LIFECYCLE_STOP_IF_IDLE,1,SYS_OK); CK(stops==1U); break;
    case 21: Query(); Acquire(1,1); hw_state=APP_RESOURCE_FAULT_DRAINING; Exchange(2,APP_LIFECYCLE_ACQUIRE,1,SYS_ERR_BUSY); CK(core.entries[0].phase==2U && starts==1U); break;
    case 22: Query(); Exchange(1,APP_LIFECYCLE_STOP_IF_IDLE,1,SYS_OK); CK(sent.completion_flags==0U && starts==0U && stops==0U); break;
    case 23: Query(); Acquire(1,1); Release(1,1); Exchange(1,APP_LIFECYCLE_STOP_IF_IDLE,1,SYS_OK); CK(stops==1U && sent.completion_flags==APP_LIFECYCLE_HARDWARE_STOPPED); break;
    case 24:
        Query(); Acquire(1,1); Release(1,1); stop_result=SYS_ERR_BUSY; Begin(1,APP_LIFECYCLE_STOP_IF_IDLE,1); Process();
        Begin(2,APP_LIFECYCLE_ACQUIRE,1); Process(); Finish(2,SYS_ERR_BUSY); CK(starts==1U && hw_state==APP_RESOURCE_STOPPING); break;
    case 25: Query(); Exchange(1,APP_LIFECYCLE_STOP_IF_IDLE,1,SYS_OK); Query(); CK(core.session_id==2U); Exchange(2,APP_LIFECYCLE_SESSION_QUERY,2,SYS_OK); CK(core.session_id==2U); break;
    case 26:
        CK(App_LifecycleTxReserveIds(&tx[0],3U,&n)==SYS_OK && n==1U);
        m=Request(1,APP_LIFECYCLE_SESSION_QUERY,1); BeginMessage(m); Process(); Finish(1,SYS_OK);
        m.identity.request_id=2U; CK(App_LifecycleTxStage(&tx[0],&m)==SYS_ERR_PROTOCOL); break;
    case 27:
        Query(); Begin(1,APP_LIFECYCLE_ACQUIRE,1); fault_mode=1U; Process();
        CK(core.entries[0].decision.phase==APP_QUAL_DECIDED && starts==1U); Process();
        send_result=SYS_ERR_NO_RESOURCE; CK(App_QualificationFlush(&core,1U,ToConsumer,NULL)==SYS_ERR_NO_RESOURCE);
        CK(core.rx[0].reply_pending.phase==APP_LIFECYCLE_TX_READY); send_result=SYS_OK; Finish(1,SYS_OK); CK(starts==1U); break;
    case 28:
        Query(); Begin(1,APP_LIFECYCLE_ACQUIRE,1); fault_mode=2U; Process();
        Begin(2,APP_LIFECYCLE_ACQUIRE,1); Process(); Finish(2,SYS_OK);
        CK(core.entries[0].decision.phase==APP_QUAL_BLOCKED && core.entries[1].phase==2U && starts==1U); break;
    case 29:
        Query(); Acquire(1,1); before=core; CK(App_QualificationGetStatus(&core,1U,&view)==SYS_OK);
        CK(memcmp(&before,&core,sizeof(core))==0 && view.phase==APP_QUAL_ACTIVE); break;
    case 30:
        Query(); Begin(1,APP_LIFECYCLE_ACQUIRE,1); fault_mode=1U; Process();
        CK(core.entries[0].phase==2U && core.entries[0].decision.phase==APP_QUAL_DECIDED && core.rx[0].replay.completed==0U);
        Process(); Finish(1,SYS_OK); CK(starts==1U); break;
    case 31:
        Query(); Acquire(1,1); drain[0]=SYS_OK; Begin(1,APP_LIFECYCLE_RELEASE,1); fault_mode=1U; Process();
        CK(core.entries[0].phase==0U && core.entries[0].decision.phase==APP_QUAL_DECIDED); n=releases[0]; Process(); Finish(1,SYS_OK); CK(releases[0]==n); break;
    case 32:
        Query(); Begin(1,APP_LIFECYCLE_ACQUIRE,1); Process(); send_result=SYS_ERR_NO_RESOURCE;
        for(i=0;i<10;i++) CK(App_QualificationFlush(&core,1U,ToConsumer,NULL)==SYS_ERR_NO_RESOURCE);
        send_result=SYS_OK; Finish(1,SYS_OK); CK(starts==1U); break;
    case 33:
        Query(); Begin(1,APP_LIFECYCLE_ACQUIRE,1); fault_mode=1U; Process(); m=original[0];
        CK(App_QualificationReceive(&core,&m,&d)==SYS_OK && d==APP_LIFECYCLE_RX_DUPLICATE_RUNNING); Process(); CK(starts==1U); break;
    case 34:
        Query(); Begin(1,APP_LIFECYCLE_ACQUIRE,1); fault_mode=2U; Process(); Begin(2,APP_LIFECYCLE_ACQUIRE,1); Process(); Finish(2,SYS_OK); CK(core.entries[1].phase==2U); break;
    case 35:
        Query(); Begin(1,APP_LIFECYCLE_ACQUIRE,1); Process(); send_result=SYS_ERR_NO_RESOURCE;
        CK(App_QualificationFlush(&core,1U,ToConsumer,NULL)==SYS_ERR_NO_RESOURCE && tx[0].pending.phase==APP_LIFECYCLE_TX_WAIT_REPLY && hw_state==APP_RESOURCE_RUNNING); break;
    case 36:
        Query(); Acquire(1,1); Release(1,1); Exchange(1,APP_LIFECYCLE_STOP_IF_IDLE,1,SYS_OK); m=original[0]; cached=sent;
        Exchange(2,APP_LIFECYCLE_SESSION_QUERY,2,SYS_OK); Acquire(2,2);
        CK(App_QualificationReceive(&core,&m,&d)==SYS_OK && d==APP_LIFECYCLE_RX_REPLAY); Process();
        CK(App_QualificationFlush(&core,1U,Discard,NULL)==SYS_OK && memcmp(&sent,&cached,sizeof(sent))==0 && starts==2U && stops==1U && hw_state==APP_RESOURCE_RUNNING); break;
    case 37:
        Begin(1,APP_LIFECYCLE_SESSION_QUERY,1); fault_mode=1U; Process(); CK(core.session_id==1U); Process(); Finish(1,SYS_OK); CK(core.session_id==1U); break;
    case 38:
        Query(); Begin(1,APP_LIFECYCLE_ACQUIRE,1); fault_mode=2U; Process(); n=starts;
        for(i=0;i<10;i++)Process(); CK(starts==n && core.entries[0].decision.phase==APP_QUAL_BLOCKED); break;
    case 39:
        Query(); Begin(1,APP_LIFECYCLE_ACQUIRE,1); fault_mode=3U; Process(); Finish(1,SYS_OK); CK(starts==1U);
        break;
    case 40:
        Query(); Begin(1,APP_LIFECYCLE_ACQUIRE,1); fault_mode=5U; Process();
        CK(core.entries[0].decision.phase==APP_QUAL_BLOCKED && core.rx[0].replay.completed==1U && core.rx[0].final_reply_enqueued==0U); break;
    case 41:
        Query(); Begin(1,APP_LIFECYCLE_ACQUIRE,1); fault_mode=2U; Process();
        for(i=0;i<100;i++)Process(); CK(core.entries[0].phase==APP_QUAL_ACTIVE && core.entries[0].decision.phase==APP_QUAL_BLOCKED && starts==1U); break;
    case 42:
        Query(); Acquire(1,1); Acquire(2,1); Begin(1,APP_LIFECYCLE_RELEASE,1); Begin(2,APP_LIFECYCLE_RELEASE,1);
        drain[0]=SYS_OK; fault_mode=1U; Process(); CK(core.entries[0].phase==0U && core.entries[1].phase==3U && stops==0U); break;
    case 43:
        Query(); Begin(1,APP_LIFECYCLE_ACQUIRE,1); fault_mode=1U; Process();
        CK(core.entries[0].decision.phase==APP_QUAL_DECIDED); Process(); Finish(1,SYS_OK);
        CK(starts==1U && core.entries[0].decision.phase==APP_QUAL_NONE); break;
    case 44:
        Query(); Begin(1,APP_LIFECYCLE_ACQUIRE,1); fault_mode=4U; Process(); CK(core.entries[0].decision.phase==APP_QUAL_BLOCKED); break;
    case 45:
        Query(); Begin(1,APP_LIFECYCLE_ACQUIRE,1); fault_mode=6U; Process(); CK(core.entries[0].decision.phase==APP_QUAL_BLOCKED); break;
    case 46:
        Query(); Begin(1,APP_LIFECYCLE_ACQUIRE,1); fault_mode=7U; Process(); Finish(1,SYS_OK); CK(starts==1U); break;
    case 47:
        Query(); Acquire(1,1); Begin(1,APP_LIFECYCLE_RELEASE,1); missing.released=NULL;
        CK(App_QualificationProcess(&core,&missing,NULL)==SYS_OK && core.entries[0].phase==APP_QUAL_RELEASING && core.rx[0].replay.completed==0U); break;
    case 48:
        Query(); Begin(1,APP_LIFECYCLE_STOP_IF_IDLE,1); missing.idle=NULL;
        CK(App_QualificationProcess(&core,&missing,NULL)==SYS_OK); Finish(1,SYS_ERR_BUSY); CK(stops==0U); break;
    case 49:
        Query(); Acquire(1,1); Release(1,1); stop_result=SYS_ERR_IO; Exchange(1,APP_LIFECYCLE_STOP_IF_IDLE,1,SYS_ERR_IO);
        CK(core.session_closed==0U && hw_state==APP_RESOURCE_FAULT_DRAINING && stops==1U); break;
    case 50:
        Query(); reentry=1U; Acquire(1,1); CK(starts==1U); break;
    case 51:
        CK(App_QualificationInit(&core)==SYS_ERR_ALREADY_EXISTS);
        CK(App_QualificationInit(NULL)==SYS_ERR_INVALID_ARG);
        CK(App_QualificationGetStatus(&core,99U,&view)==SYS_ERR_INVALID_ARG);
        CK(App_QualificationNextGeneration(&n,&n)==SYS_ERR_INVALID_ARG);
        CK(App_QualificationProcess(&core,NULL,NULL)==SYS_ERR_INVALID_ARG); break;
    case 52:
        Query(); Begin(1,APP_LIFECYCLE_ACQUIRE,1); Process(); core.rx[0].reply_pending.message.operation=APP_LIFECYCLE_RELEASE;
        CK(App_QualificationFlush(&core,1U,ToConsumer,NULL)==SYS_ERR_PROTOCOL && core.entries[0].decision.phase==APP_QUAL_BLOCKED); break;
    case 53:
        Query(); core.session_id=UINT32_MAX; core.session_closed=1U;
        Exchange(1,APP_LIFECYCLE_SESSION_QUERY,1,SYS_ERR_NO_RESOURCE); CK(core.session_id==UINT32_MAX && starts==0U); break;
    case 54:
        Query(); Begin(1,APP_LIFECYCLE_ACQUIRE,1); Process(); m=original[0];m.identity.consumer_generation++;
        CK(App_QualificationReceive(&core,&m,&d)==SYS_ERR_PROTOCOL && starts==1U); break;
    case 55:
        for(i=1;i<=512U;i++)
        {
            Query(); Acquire(1,i); Release(1,i); Exchange(1,APP_LIFECYCLE_STOP_IF_IDLE,i,SYS_OK);
        }
        CK(core.session_id==512U && starts==512U && stops==512U && sizeof(core)==824U); break;
    case 56:
        Query(); Begin(1,APP_LIFECYCLE_ACQUIRE,1); fault_mode=1U; Process();
        core.rx[0].replay.request.identity.consumer_generation++;
        Process(); CK(core.entries[0].decision.phase==APP_QUAL_BLOCKED && starts==1U); break;
    case 57:
        Query(); Begin(1,APP_LIFECYCLE_ACQUIRE,1); fault_mode=1U; Process();
        CK(Qual_Reply(&core,0U,&m)==SYS_OK);
        CK(App_LifecycleRxComplete(&core.rx[0],&m)==SYS_OK);
        CK(App_LifecycleRxComplete(&core.rx[0],&m)==SYS_ERR_ALREADY_EXISTS);
        Process(); Finish(1,SYS_OK); CK(starts==1U); break;
    case 58:
        before=core;
        CK(App_QualificationGetStatus(&core,1U,(AppQualificationStatus_t *)&core)==SYS_ERR_INVALID_ARG);
        m=Request(1,APP_LIFECYCLE_SESSION_QUERY,1);
        CK(App_QualificationReceive(&core,&m,&core.initialized)==SYS_ERR_INVALID_ARG);
        CK(memcmp(&before,&core,sizeof(core))==0); break;
    case 59:
        Query(); Acquire(1,1); Release(1,1); Begin(1,APP_LIFECYCLE_STOP_IF_IDLE,1);
        idle_result=SYS_ERR_IO; Process(); Finish(1,SYS_ERR_BUSY); CK(stops==0U); break;
    case 60:
        Query(); cached=sent; Begin(1,APP_LIFECYCLE_ACQUIRE,2); before=core;
        CK(App_LifecycleTxAcceptReply(&tx[0],&cached)==SYS_ERR_PROTOCOL);
        CK(tx[0].pending.phase==APP_LIFECYCLE_TX_WAIT_REPLY && memcmp(&before,&core,sizeof(core))==0);
        Process(); Finish(1,SYS_OK); break;
    case 61:
        memset(&before,0,sizeof(before));before.session_id=8U;
        CK(App_QualificationInit(&before)==SYS_ERR_INVALID_STATE && before.session_id==8U);
        CK(App_QualificationNextGeneration(NULL,&n)==SYS_ERR_INVALID_ARG); break;
    case 62:
        CK(App_QualificationNextGeneration(&generation[0],&n)==SYS_OK && n==1U);
        m=Request(1,APP_LIFECYCLE_SESSION_QUERY,n);CK(App_LifecycleTxStage(&tx[0],&m)==SYS_OK);
        CK(App_LifecycleTxCancelReady(&tx[0],&m)==SYS_OK);
        CK(App_QualificationNextGeneration(&generation[0],&n)==SYS_OK && n==2U);
        Exchange(1,APP_LIFECYCLE_SESSION_QUERY,n,SYS_OK);CK(generation[0]==2U);break;
    case 63:
        Query(); Acquire(1,1); Release(1,1);
        Begin(2,APP_LIFECYCLE_ACQUIRE,1);Begin(1,APP_LIFECYCLE_STOP_IF_IDLE,1);Process();
        Finish(1,SYS_ERR_BUSY);Finish(2,SYS_OK);CK(stops==0U && starts==1U);break;
    case 64:
        Query(); Acquire(1,1); Release(1,1);idle_result=SYS_ERR_BUSY;
        Exchange(1,APP_LIFECYCLE_STOP_IF_IDLE,1,SYS_ERR_BUSY);CK(stops==0U && hw_state==APP_RESOURCE_RUNNING);break;
    case 65:
        hw_state=99U;Exchange(1,APP_LIFECYCLE_SESSION_QUERY,1,SYS_ERR_PROTOCOL);
        CK(core.session_id==0U && starts==0U && stops==0U);break;
    case 66:
        Query(); Acquire(1,1); drain[0]=SYS_ERR_IO;Begin(1,APP_LIFECYCLE_RELEASE,1);Process();
        CK(App_QualificationGetStatus(&core,1U,&view)==SYS_OK && view.phase==APP_QUAL_RELEASING && view.submit_error==SYS_ERR_IO);
        CK(core.entries[0].grant_generation==1U && core.rx[0].replay.completed==0U);break;
    default: CK(0); break;
    }
    return (int)g_failure;
}

//纯模型不借用真实App_Camera，独立适配测试另行验证私有状态所有权
SysResult_t App_CameraQualificationAccess(AppCameraQualification_t **c,
    const AppQualificationResourceOps_t **o, bool preparing)
{ (void)c; (void)o; (void)preparing; return SYS_ERR_INVALID_STATE; }

//纯模型不接入真实任务；App_Camera适配专项复用原B1任务桩
uint32_t Platform_SyncGetException(void) { return 0U; }
bool App_MessagesIsOwner(RuntimeEventTarget_t target) { (void)target; return false; }
