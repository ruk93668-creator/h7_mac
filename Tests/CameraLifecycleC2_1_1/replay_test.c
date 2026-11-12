//复用真实FreeRTOS队列和当前消息适配，任务身份只在测试中注入
#include "../CameraLifecycleC1/transport_test.c"
#include "app_camera_lifecycle_replay.h"

//管理对象只存在于独立验收程序，不进入正式业务静态区
static AppLifecycleTxContext_t tx[4], tx_before;
static AppLifecycleRxContext_t rx[4], rx_before;
static uint32_t business_calls, send_calls, hook_mode, hook_ok;
static int32_t forced_send;
static uint32_t fail_begin;

//包装实际ReplayBegin，仅对临时构造失败注入返回值，不复制生产实现
#define App_LifecycleReplayBegin C2_RealReplayBegin
#include "../../CM7/Application/app_camera_lifecycle.c"
#undef App_LifecycleReplayBegin
SysResult_t App_LifecycleReplayBegin(AppLifecycleReplay_t *record, const AppLifecycleMessage_t *message)
{
    if (fail_begin != 0U)
        return SYS_ERR_IO;
    return C2_RealReplayBegin(record, message);
}
static uint32_t g_layout[12];

//构造四个方向的合法请求，不依赖真实Camera资源
static AppLifecycleMessage_t Request(uint32_t domain, uint32_t id)
{
    AppLifecycleMessage_t m = Life((domain & 1U) + 1U, domain < 2U ? APP_LIFECYCLE_ACQUIRE : APP_LIFECYCLE_RELEASE_REQUEST, APP_LIFECYCLE_REQUEST);
    m.identity.request_id = id;
    return m;
}

//根据消息目标选择既有三条通道
static AppLifecycleChannel_t Channel(uint32_t target)
{
    return (AppLifecycleChannel_t)(target == APP_LIFECYCLE_CAMERA ? 0U : target == APP_LIFECYCLE_USB ? 1U : 2U);
}

//调用管理接口的回调重入必须被拒绝，回调仍只报告真实入队结果
static SysResult_t Send(void *context, const AppLifecycleMessage_t *m)
{
    uint32_t d = *(uint32_t *)context, n = 99U, disposition;
    AppLifecycleMessage_t out;
    AppLifecycleReplayStatus_t status;
    send_calls++;
    if (hook_mode == 1U)
    {
        hook_ok = App_LifecycleTxReserveIds(&tx[d], 1U, &n) == SYS_ERR_BUSY && n == 99U &&
            App_LifecycleTxInit(&tx[d], tx[d].source, tx[d].target, tx[d].consumer_id) == SYS_ERR_BUSY &&
            App_LifecycleTxStage(&tx[d], m) == SYS_ERR_BUSY &&
            App_LifecycleTxFlush(&tx[d], Send, context) == SYS_ERR_BUSY &&
            App_LifecycleTxCancelReady(&tx[d], m) == SYS_ERR_BUSY &&
            App_LifecycleTxConfirmConsumed(&tx[d], m) == SYS_ERR_BUSY &&
            App_LifecycleTxAcceptReply(&tx[d], m) == SYS_ERR_BUSY &&
            App_LifecycleTxGetCompleted(&tx[d], &out) == SYS_ERR_BUSY;
    }
    if (hook_mode == 2U)
    {
        out = rx[d].replay.request;
        hook_ok = App_LifecycleRxGetStatus(&rx[d], &status) == SYS_ERR_BUSY &&
            App_LifecycleRxReceiveRequest(&rx[d], &out, &disposition) == SYS_ERR_BUSY &&
            App_LifecycleRxPromote(&rx[d], &out) == SYS_ERR_BUSY &&
            App_LifecycleRxComplete(&rx[d], m) == SYS_ERR_BUSY &&
            App_LifecycleRxFlushReply(&rx[d], Send, context) == SYS_ERR_BUSY &&
            App_LifecycleRxInit(&rx[d], rx[d].source, rx[d].target, rx[d].consumer_id) == SYS_ERR_BUSY;
    }
    if (forced_send != SYS_OK)
        return forced_send;
    owner = Lifecycle_Task(m->source);
    return App_LifecycleMessagesSend(Channel(m->target), m);
}

//初始化四个独立编号域及原三条静态队列
static uint32_t Setup(void)
{
    uint32_t d, first;
    AppLifecycleMessage_t m;
    CHECK(App_LifecycleMessagesInit() == SYS_OK);
    scheduler = taskSCHEDULER_RUNNING;
    for (d = 0U; d < 4U; d++)
    {
        m = Request(d, 1U);
        CHECK(App_LifecycleTxInit(&tx[d], m.source, m.target, m.identity.consumer_id) == SYS_OK);
        CHECK(App_LifecycleRxInit(&rx[d], m.source, m.target, m.identity.consumer_id) == SYS_OK);
        CHECK(App_LifecycleTxReserveIds(&tx[d], 100000U, &first) == SYS_OK && first == 1U);
    }
    return 0;
}

//接收消息只走真实消息通道，不以通知次数代替队列数据
static bool Receive(uint32_t target, AppLifecycleMessage_t *m)
{
    owner = Lifecycle_Task(target);
    return App_LifecycleMessagesReceive(Channel(target), m);
}

//建立当前执行事务，业务桩只在Promote首次成功后计数
static uint32_t Begin(uint32_t d, uint32_t id)
{
    uint32_t disposition;
    AppLifecycleMessage_t m = Request(d, id), out;
    CHECK(App_LifecycleRxReceiveRequest(&rx[d], &m, &disposition) == SYS_OK);
    CHECK(disposition == APP_LIFECYCLE_RX_CANDIDATE);
    CHECK(App_LifecycleRxPromote(&rx[d], &out) == SYS_OK);
    CHECK(memcmp(&out, &m, sizeof(m)) == 0);
    business_calls++;
    CHECK(App_LifecycleRxPromote(&rx[d], &out) == SYS_ERR_NOT_FOUND);
    return 0;
}

//固定当前回复，可选择不入队以检查退休边界
static uint32_t Finish(uint32_t d, bool enqueue)
{
    AppLifecycleMessage_t reply;
    CHECK(App_LifecycleMakeReply(&rx[d].replay.request, rx[d].replay.request.identity.session_id == 0U ? 9U : rx[d].replay.request.identity.session_id, SYS_OK, &reply) == SYS_OK);
    CHECK(App_LifecycleRxComplete(&rx[d], &reply) == SYS_OK);
    if (enqueue)
        CHECK(App_LifecycleRxFlushReply(&rx[d], Send, &d) == SYS_OK);
    return 0;
}

//执行一个完整协议往返，帧资源和业务启停均未接入
static uint32_t Exchange(uint32_t d, uint32_t id)
{
    uint32_t disposition;
    AppLifecycleMessage_t m = Request(d, id), out, reply;
    //循环覆盖查询、申请、释放及空闲停止，不实际执行业务资源操作
    m.identity.consumer_generation = 1U + id / 16U;
    m.identity.session_id = 1U + id / 64U;
    if (d < 2U)
    {
        m.operation = 1U + id % 4U;
        if (m.operation == APP_LIFECYCLE_SESSION_QUERY)
            m.identity.session_id = 0U;
    }
    else if ((id & 1U) != 0U)
    {
        m.operation = APP_LIFECYCLE_FAULT_NOTICE;
        m.result = SYS_ERR_IO;
    }
    CHECK(App_LifecycleTxStage(&tx[d], &m) == SYS_OK);
    CHECK(App_LifecycleTxFlush(&tx[d], Send, &d) == SYS_OK);
    CHECK(Receive(m.target, &out));
    CHECK(App_LifecycleRxReceiveRequest(&rx[d], &out, &disposition) == SYS_OK);
    CHECK(App_LifecycleRxPromote(&rx[d], &out) == SYS_OK);
    business_calls++;
    CHECK(Finish(d, true) == 0U);
    CHECK(Receive(m.source, &reply));
    CHECK(App_LifecycleTxAcceptReply(&tx[d], &reply) == SYS_OK);
    CHECK(App_LifecycleTxGetCompleted(&tx[d], &out) == SYS_OK);
    CHECK(memcmp(&reply, &out, sizeof(reply)) == 0);
    CHECK(App_LifecycleTxConfirmConsumed(&tx[d], &m) == SYS_OK);
    return 0;
}

//逐项验证协议窗口、身份、回滚和真实队列交错
uint32_t C2_ReplayRun(uint32_t id)
{
    uint32_t d = 0U, n, first, disposition = 99U;
    AppLifecycleMessage_t m, r, out, before_message;
    AppLifecycleReplayStatus_t status;
    CHECK(Setup() == 0U);
    m = Request(d, 10U);
    if (id >= 100U && id < 356U)
    {
        //每个样本64次，四方向交错合计16384次循环/优化级别
        d = (id - 100U) % 4U;
        for (n = 1U; n <= 64U; n++)
        {
            CHECK(Exchange(d, n * 2U) == 0U);
            r = Request(d, n * 2U - 1U);
            CHECK(App_LifecycleRxReceiveRequest(&rx[d], &r, &disposition) == SYS_ERR_PROTOCOL);
            CHECK(disposition == APP_LIFECYCLE_RX_STALE);
            CHECK(rx[d].candidate_state == 0U && rx[d].protocol_fault == 0U);
        }
        CHECK(business_calls == 64U && rx[d].active_request_id == 128U && rx[d].highest_retired_request_id == 126U);
        CHECK(sizeof(tx) == 576U && sizeof(rx) == 1328U);
        return 0;
    }
    if (id >= 60U && id < 74U)
    {
        //逐字段变更确认凭据，任何字段不同都不能消费当前COMPLETE
        CHECK(App_LifecycleTxStage(&tx[d], &m) == SYS_OK);
        CHECK(App_LifecycleTxFlush(&tx[d], Send, &d) == SYS_OK);
        CHECK(App_LifecycleMakeReply(&m, 9U, SYS_OK, &r) == SYS_OK);
        CHECK(App_LifecycleTxAcceptReply(&tx[d], &r) == SYS_OK);
        ((uint32_t *)&m)[id - 60U] ^= 1U;
        tx_before = tx[d];
        CHECK(App_LifecycleTxConfirmConsumed(&tx[d], &m) == SYS_ERR_PROTOCOL);
        CHECK(memcmp(&tx_before, &tx[d], sizeof(tx_before)) == 0);
        return 0;
    }
    switch (id)
    {
    case 0:
        CHECK(sizeof(AppMessage_t)==52U && sizeof(AppLifecycleMessage_t)==56U);
        CHECK(sizeof(AppLifecycleReplay_t)==120U && sizeof(AppLifecyclePending_t)==116U);
        CHECK(sizeof(AppLifecycleTxContext_t)==144U && sizeof(AppLifecycleRxContext_t)==332U);
        CHECK(offsetof(AppLifecycleTxContext_t,pending)==28U && offsetof(AppLifecycleRxContext_t,replay)==40U);
        CHECK(offsetof(AppLifecycleRxContext_t,reply_pending)==160U && offsetof(AppLifecycleRxContext_t,candidate)==276U);
        g_layout[0]=sizeof(tx);g_layout[1]=sizeof(rx);g_layout[2]=sizeof(s_lifecycle_storage);g_layout[3]=sizeof(s_lifecycle_queues);g_layout[4]=sizeof(s_lifecycle_handles);
        CHECK(g_layout[0]+g_layout[1]==1904U && g_layout[2]==672U && g_layout[3]==216U && g_layout[4]==12U);break;
    case 1:
        CHECK(Begin(d,10U)==0U);
        CHECK(App_LifecycleRxReceiveRequest(&rx[d],&m,&disposition)==SYS_OK && disposition==APP_LIFECYCLE_RX_DUPLICATE_RUNNING);
        CHECK(business_calls==1U && rx[d].highest_retired_request_id==0U);break;
    case 2:
        CHECK(Begin(d,10U)==0U);CHECK(Finish(d,true)==0U);r=rx[d].replay.reply;
        CHECK(Receive(m.source,&out));CHECK(memcmp(&r,&out,sizeof(r))==0);
        CHECK(App_LifecycleRxReceiveRequest(&rx[d],&m,&disposition)==SYS_OK && disposition==APP_LIFECYCLE_RX_REPLAY);
        CHECK(App_LifecycleRxFlushReply(&rx[d],Send,&d)==SYS_OK);CHECK(Receive(m.source,&out));
        CHECK(memcmp(&r,&out,sizeof(r))==0 && business_calls==1U);break;
    case 3:
        CHECK(Begin(d,10U)==0U);m.identity.consumer_generation++;
        rx_before=rx[d];CHECK(App_LifecycleRxReceiveRequest(&rx[d],&m,&disposition)==SYS_ERR_PROTOCOL);
        CHECK(disposition==APP_LIFECYCLE_RX_CONFLICT && memcmp(&rx_before,&rx[d],sizeof(rx_before))==0);break;
    case 4:
    case 5:
        CHECK(Begin(d,10U)==0U);if(id==5U)CHECK(Finish(d,false)==0U);
        m.identity.request_id=11U;CHECK(App_LifecycleRxReceiveRequest(&rx[d],&m,&disposition)==SYS_ERR_PROTOCOL);
        CHECK(disposition==APP_LIFECYCLE_RX_QUARANTINED && rx[d].candidate_state==2U && rx[d].protocol_fault==1U);
        if(id==4U)CHECK(Finish(d,false)==0U);
        CHECK(App_LifecycleRxFlushReply(&rx[d],Send,&d)==SYS_OK);
        rx_before=rx[d];CHECK(App_LifecycleRxPromote(&rx[d],&out)==SYS_ERR_PROTOCOL);
        CHECK(memcmp(&rx_before,&rx[d],sizeof(rx_before))==0 && rx[d].highest_retired_request_id==0U);break;
    case 6:
        CHECK(Begin(d,10U)==0U);CHECK(Finish(d,true)==0U);m.identity.request_id=20U;
        CHECK(App_LifecycleRxReceiveRequest(&rx[d],&m,&disposition)==SYS_OK);CHECK(rx[d].highest_retired_request_id==0U);
        CHECK(App_LifecycleRxPromote(&rx[d],&out)==SYS_OK);CHECK(rx[d].highest_retired_request_id==10U && rx[d].active_request_id==20U);
        CHECK(rx[d].replay.completed==0U && rx[d].final_reply_enqueued==0U);break;
    case 7:
    case 8:
        CHECK(Begin(d,10U)==0U);CHECK(Finish(d,true)==0U);m.identity.request_id=20U;
        CHECK(App_LifecycleRxReceiveRequest(&rx[d],&m,&disposition)==SYS_OK);CHECK(App_LifecycleRxPromote(&rx[d],&out)==SYS_OK);
        m.identity.request_id=id==7U?10U:15U;rx_before=rx[d];
        CHECK(App_LifecycleRxReceiveRequest(&rx[d],&m,&disposition)==SYS_ERR_PROTOCOL && disposition==APP_LIFECYCLE_RX_STALE);
        CHECK(memcmp(&rx_before,&rx[d],sizeof(rx_before))==0);break;
    case 9:
        CHECK(Begin(d,10U)==0U);CHECK(Finish(d,true)==0U);
        CHECK(App_LifecycleRxReceiveRequest(&rx[d],&m,&disposition)==SYS_OK);
        m.identity.request_id=20U;CHECK(App_LifecycleRxReceiveRequest(&rx[d],&m,&disposition)==SYS_OK);
        rx_before=rx[d];CHECK(App_LifecycleRxPromote(&rx[d],&out)==SYS_ERR_BUSY);CHECK(memcmp(&rx_before,&rx[d],sizeof(rx_before))==0);
        CHECK(App_LifecycleRxFlushReply(&rx[d],Send,&d)==SYS_OK);CHECK(App_LifecycleRxPromote(&rx[d],&out)==SYS_OK);break;
    case 10:
        CHECK(Begin(d,10U)==0U);CHECK(Finish(d,true)==0U);m.identity.request_id=20U;
        CHECK(App_LifecycleRxReceiveRequest(&rx[d],&m,&disposition)==SYS_OK);rx_before=rx[d];
        CHECK(App_LifecycleRxReceiveRequest(&rx[d],&m,&disposition)==SYS_OK && disposition==APP_LIFECYCLE_RX_DUPLICATE_CANDIDATE);
        CHECK(memcmp(&rx_before,&rx[d],sizeof(rx_before))==0);break;
    case 11:
    case 12:
        CHECK(Begin(d,10U)==0U);CHECK(Finish(d,true)==0U);m.identity.request_id=20U;
        CHECK(App_LifecycleRxReceiveRequest(&rx[d],&m,&disposition)==SYS_OK);before_message=rx[d].candidate;
        if(id==11U)m.identity.consumer_generation++;else m.identity.request_id++;
        CHECK(App_LifecycleRxReceiveRequest(&rx[d],&m,&disposition)==SYS_ERR_PROTOCOL && disposition==APP_LIFECYCLE_RX_CONFLICT);
        CHECK(memcmp(&before_message,&rx[d].candidate,sizeof(m))==0 && rx[d].candidate_state==2U);
        CHECK(App_LifecycleRxPromote(&rx[d],&out)==SYS_ERR_PROTOCOL);break;
    case 13:
        m.operation=APP_LIFECYCLE_SESSION_QUERY;m.identity.session_id=0U;
        CHECK(App_LifecycleRxReceiveRequest(&rx[d],&m,&disposition)==SYS_OK);CHECK(App_LifecycleRxPromote(&rx[d],&out)==SYS_OK);CHECK(Finish(d,true)==0U);
        CHECK(App_LifecycleRxReceiveRequest(&rx[d],&m,&disposition)==SYS_OK);CHECK(rx[d].replay.reply.identity.session_id==9U);
        CHECK(App_LifecycleRxFlushReply(&rx[d],Send,&d)==SYS_OK);r=m;r.identity.request_id=11U;r.identity.consumer_generation++;
        CHECK(App_LifecycleRxReceiveRequest(&rx[d],&r,&disposition)==SYS_OK);CHECK(App_LifecycleRxPromote(&rx[d],&out)==SYS_OK);
        CHECK(App_LifecycleRxReceiveRequest(&rx[d],&m,&disposition)==SYS_ERR_PROTOCOL);break;
    case 14:
        CHECK(Begin(d,10U)==0U);CHECK(Finish(d,true)==0U);m.identity.request_id=20U;m.identity.session_id=99U;m.identity.consumer_generation=8U;
        CHECK(App_LifecycleRxReceiveRequest(&rx[d],&m,&disposition)==SYS_OK);CHECK(App_LifecycleRxPromote(&rx[d],&out)==SYS_OK);
        CHECK(rx[d].highest_retired_request_id==10U);r=Request(d,10U);CHECK(App_LifecycleRxReceiveRequest(&rx[d],&r,&disposition)==SYS_ERR_PROTOCOL);break;
    case 15:
        CHECK(Begin(d,10U)==0U);rx_before=rx[d];m.identity.request_id=UINT32_MAX;m.version++;
        CHECK(App_LifecycleRxReceiveRequest(&rx[d],&m,&disposition)==SYS_ERR_INVALID_ARG && disposition==99U);
        CHECK(memcmp(&rx_before,&rx[d],sizeof(rx_before))==0);break;
    case 16:
        CHECK(App_LifecycleTxStage(&tx[d],&m)==SYS_OK);CHECK(tx[d].issued_through==10U);
        CHECK(App_LifecycleTxCancelReady(&tx[d],&m)==SYS_OK);CHECK(tx[d].issued_through==10U && tx[d].pending.phase==0U);
        tx_before=tx[d];CHECK(App_LifecycleTxStage(&tx[d],&m)==SYS_ERR_PROTOCOL);CHECK(memcmp(&tx_before,&tx[d],sizeof(tx_before))==0);
        CHECK(App_LifecycleTxCancelReady(&tx[d],&m)==SYS_ERR_INVALID_STATE);m.identity.request_id=11U;CHECK(App_LifecycleTxStage(&tx[d],&m)==SYS_OK);break;
    case 17:
        CHECK(App_LifecycleTxStage(&tx[d],&m)==SYS_OK);CHECK(App_LifecycleTxFlush(&tx[d],Send,&d)==SYS_OK);
        tx_before=tx[d];CHECK(App_LifecycleTxFlush(&tx[d],Send,&d)==SYS_ERR_BUSY);
        CHECK(App_LifecycleTxCancelReady(&tx[d],&m)==SYS_ERR_INVALID_STATE);
        r=Request(d,11U);CHECK(App_LifecycleTxStage(&tx[d],&r)==SYS_ERR_BUSY);
        CHECK(App_LifecycleTxReserveIds(&tx[d],1U,&first)==SYS_ERR_BUSY);
        CHECK(App_LifecycleTxConfirmConsumed(&tx[d],&m)==SYS_ERR_INVALID_STATE);
        CHECK(memcmp(&tx_before,&tx[d],sizeof(tx_before))==0);break;
    case 18:
        CHECK(App_LifecycleTxStage(&tx[d],&m)==SYS_OK);CHECK(App_LifecycleTxFlush(&tx[d],Send,&d)==SYS_OK);
        CHECK(App_LifecycleMakeReply(&m,9U,SYS_OK,&r)==SYS_OK);CHECK(App_LifecycleTxAcceptReply(&tx[d],&r)==SYS_OK);
        CHECK(App_LifecycleTxGetCompleted(&tx[d],&out)==SYS_OK);before_message=Request(d,11U);
        CHECK(App_LifecycleTxStage(&tx[d],&before_message)==SYS_ERR_BUSY);CHECK(App_LifecycleTxConfirmConsumed(&tx[d],&m)==SYS_OK);
        CHECK(App_LifecycleTxConfirmConsumed(&tx[d],&m)==SYS_ERR_INVALID_STATE);CHECK(App_LifecycleTxStage(&tx[d],&before_message)==SYS_OK);break;
    case 19:
        for(n=0U;n<4U;n++)CHECK(Exchange(n,10U)==0U);
        for(n=0U;n<4U;n++){CHECK(tx[n].issued_through==10U && rx[n].active_request_id==10U);}
        CHECK(Exchange(0U,20U)==0U);CHECK(rx[0].highest_retired_request_id==10U && rx[1].highest_retired_request_id==0U);break;
    case 20:
        CHECK(App_LifecycleTxStage(&tx[d],&m)==SYS_OK);tx_before=tx[d];
        CHECK(App_LifecycleTxInit(&tx[d],m.source,m.target,m.identity.consumer_id)==SYS_ERR_ALREADY_EXISTS);
        CHECK(memcmp(&tx_before,&tx[d],sizeof(tx_before))==0);CHECK(Begin(d,10U)==0U);rx_before=rx[d];
        CHECK(App_LifecycleRxInit(&rx[d],m.source,m.target,m.identity.consumer_id)==SYS_ERR_ALREADY_EXISTS);
        CHECK(memcmp(&rx_before,&rx[d],sizeof(rx_before))==0);break;
    case 21:
        tx_before=tx[d];m.identity.request_id=100001U;CHECK(App_LifecycleTxStage(&tx[d],&m)==SYS_ERR_PROTOCOL);
        CHECK(memcmp(&tx_before,&tx[d],sizeof(tx_before))==0);m=Request(d,10U);m.version++;
        CHECK(App_LifecycleTxStage(&tx[d],&m)==SYS_ERR_INVALID_ARG && tx[d].issued_through==0U);break;
    case 22:
        CHECK(App_LifecycleTxReserveIds(&tx[d],UINT32_MAX-100000U,&first)==SYS_OK && first==100001U);
        tx_before=tx[d];first=123U;CHECK(App_LifecycleTxReserveIds(&tx[d],1U,&first)==SYS_ERR_NO_RESOURCE && first==123U);
        CHECK(memcmp(&tx_before,&tx[d],sizeof(tx_before))==0);CHECK(Exchange(d,UINT32_MAX)==0U);
        m.identity.request_id=1U;CHECK(App_LifecycleTxStage(&tx[d],&m)==SYS_ERR_PROTOCOL);break;
    case 23:
    case 24:
    case 25:
        CHECK(Begin(d,10U)==0U);CHECK(Finish(d,true)==0U);m.identity.request_id=20U;
        CHECK(App_LifecycleRxReceiveRequest(&rx[d],&m,&disposition)==SYS_OK);
        if(id==23U)rx[d].candidate.version++;
        if(id==24U)rx[d].candidate.identity.request_id=5U;
        if(id==25U)rx[d].candidate.target=APP_LIFECYCLE_STORAGE;
        rx_before=rx[d];memset(&out,0x5A,sizeof(out));before_message=out;
        CHECK(App_LifecycleRxPromote(&rx[d],&out)==SYS_ERR_PROTOCOL);
        CHECK(memcmp(&rx_before,&rx[d],sizeof(rx_before))==0 && memcmp(&out,&before_message,sizeof(out))==0);break;
    case 26:
        CHECK(App_LifecycleTxStage(&tx[d],&m)==SYS_OK);hook_mode=1U;
        CHECK(App_LifecycleTxFlush(&tx[d],Send,&d)==SYS_OK && hook_ok==1U);break;
    case 27:
        CHECK(Begin(d,10U)==0U);CHECK(Finish(d,false)==0U);hook_mode=2U;
        CHECK(App_LifecycleRxFlushReply(&rx[d],Send,&d)==SYS_OK && hook_ok==1U);break;
    case 28:
        CHECK(Begin(d,10U)==0U);CHECK(App_LifecycleMakeReply(&m,9U,SYS_ERR_BUSY,&r)==SYS_OK);
        CHECK(App_LifecycleRxComplete(&rx[d],&r)==SYS_OK);out=r;out.result=SYS_OK;rx_before=rx[d];
        CHECK(App_LifecycleRxComplete(&rx[d],&out)==SYS_ERR_PROTOCOL);CHECK(memcmp(&rx_before,&rx[d],sizeof(rx_before))==0);
        CHECK(App_LifecycleRxFlushReply(&rx[d],Send,&d)==SYS_OK);CHECK(Receive(m.source,&out));CHECK(memcmp(&r,&out,sizeof(r))==0);break;
    case 29:
        CHECK(Begin(d,10U)==0U);CHECK(App_LifecycleMakeReply(&m,9U,SYS_ERR_IO,&r)==SYS_OK);
        CHECK(App_LifecycleRxComplete(&rx[d],&r)==SYS_OK);CHECK(App_LifecycleRxComplete(&rx[d],&r)==SYS_ERR_ALREADY_EXISTS);
        CHECK(App_LifecycleRxFlushReply(&rx[d],Send,&d)==SYS_OK);CHECK(Receive(m.source,&out));CHECK(out.result==SYS_ERR_IO);break;
    case 30:
        owner=RUNTIME_TARGET_USB;for(n=0;n<4U;n++)CHECK(App_LifecycleMessagesSend(APP_LIFECYCLE_CHANNEL_CAMERA,&m)==SYS_OK);
        CHECK(App_LifecycleTxStage(&tx[d],&m)==SYS_OK);CHECK(App_LifecycleTxFlush(&tx[d],Send,&d)==SYS_ERR_NO_RESOURCE);
        CHECK(tx[d].pending.phase==APP_LIFECYCLE_TX_READY && tx[d].issued_through==10U);
        CHECK(Receive(m.target,&out));CHECK(App_LifecycleTxFlush(&tx[d],Send,&d)==SYS_OK);
        CHECK(tx[d].pending.phase==APP_LIFECYCLE_TX_WAIT_REPLY && uxQueueMessagesWaiting(s_lifecycle_handles[0])==4U);break;
    case 31:
        CHECK(Begin(d,10U)==0U);CHECK(Finish(d,false)==0U);r=rx[d].replay.reply;owner=RUNTIME_TARGET_CAMERA;
        for(n=0;n<4U;n++)CHECK(App_LifecycleMessagesSend(APP_LIFECYCLE_CHANNEL_USB,&r)==SYS_OK);
        CHECK(App_LifecycleRxFlushReply(&rx[d],Send,&d)==SYS_ERR_NO_RESOURCE);
        CHECK(rx[d].final_reply_enqueued==0U && rx[d].reply_pending.phase==1U);
        CHECK(Receive(m.source,&out));CHECK(App_LifecycleRxFlushReply(&rx[d],Send,&d)==SYS_OK);
        CHECK(rx[d].final_reply_enqueued==1U && rx[d].highest_retired_request_id==0U);break;
    case 32:
        notify_fails=true;CHECK(Exchange(d,10U)==0U);CHECK(business_calls==1U);
        CHECK(App_LifecycleTxFlush(&tx[d],Send,&d)==SYS_ERR_INVALID_STATE);
        CHECK(uxQueueMessagesWaiting(s_lifecycle_handles[0])==0U && uxQueueMessagesWaiting(s_lifecycle_handles[1])==0U);break;
    case 33:
        CHECK(Begin(d,10U)==0U);CHECK(Finish(d,true)==0U);CHECK(Receive(m.source,&out));
        CHECK(App_LifecycleRxReceiveRequest(&rx[d],&m,&disposition)==SYS_OK);r=rx[d].replay.reply;owner=RUNTIME_TARGET_CAMERA;
        for(n=0;n<4U;n++)CHECK(App_LifecycleMessagesSend(APP_LIFECYCLE_CHANNEL_USB,&r)==SYS_OK);
        CHECK(App_LifecycleRxFlushReply(&rx[d],Send,&d)==SYS_ERR_NO_RESOURCE);
        m.identity.request_id=20U;CHECK(App_LifecycleRxReceiveRequest(&rx[d],&m,&disposition)==SYS_OK);
        CHECK(rx[d].candidate_state==1U);CHECK(App_LifecycleRxPromote(&rx[d],&out)==SYS_ERR_BUSY);
        CHECK(Receive(m.source,&out));CHECK(App_LifecycleRxFlushReply(&rx[d],Send,&d)==SYS_OK);
        CHECK(App_LifecycleRxPromote(&rx[d],&out)==SYS_OK && rx[d].highest_retired_request_id==10U);break;
    case 34:
        CHECK(App_LifecycleTxStage(&tx[d],&m)==SYS_OK);CHECK(App_LifecycleTxStage(&tx[d],&m)==SYS_ERR_ALREADY_EXISTS);
        m.identity.request_id++;CHECK(App_LifecycleTxStage(&tx[d],&m)==SYS_ERR_BUSY && tx[d].issued_through==10U);break;
    case 35:
        CHECK(Begin(d,10U)==0U);CHECK(Finish(d,true)==0U);rx_before=rx[d];
        CHECK(App_LifecycleReplayRetire(&rx[d].replay)==SYS_ERR_NOT_SUPPORTED);CHECK(memcmp(&rx_before,&rx[d],sizeof(rx_before))==0);break;
    case 36:
        CHECK(Begin(d,10U)==0U);CHECK(App_LifecycleRxGetStatus(&rx[d],&status)==SYS_OK);
        CHECK(status.active_request_id==10U && status.completed==0U && status.highest_retired_request_id==0U);break;
    case 37:
        CHECK(App_LifecycleTxInit(NULL,m.source,m.target,1U)==SYS_ERR_INVALID_ARG);
        CHECK(App_LifecycleRxInit(NULL,m.source,m.target,1U)==SYS_ERR_INVALID_ARG);
        CHECK(App_LifecycleTxFlush(NULL,Send,&d)==SYS_ERR_INVALID_ARG);
        CHECK(App_LifecycleRxReceiveRequest(NULL,&m,&disposition)==SYS_ERR_INVALID_ARG);
        CHECK(App_LifecycleRxPromote(NULL,&out)==SYS_ERR_INVALID_ARG);
        CHECK(App_LifecycleTxReserveIds(&tx[d],1U,NULL)==SYS_ERR_INVALID_ARG);
        CHECK(App_LifecycleRxGetStatus(&rx[d],NULL)==SYS_ERR_INVALID_ARG);break;
    case 38:
        CHECK(App_LifecycleTxReserveIds(&tx[d],1U,&tx[d].issued_through)==SYS_ERR_INVALID_ARG);
        CHECK(App_LifecycleTxGetCompleted(&tx[d],&tx[d].pending.reply)==SYS_ERR_INVALID_ARG);
        CHECK(App_LifecycleRxReceiveRequest(&rx[d],&m,&rx[d].active_request_id)==SYS_ERR_INVALID_ARG);
        CHECK(App_LifecycleRxReceiveRequest(&rx[d],&m,&m.version)==SYS_ERR_INVALID_ARG);
        CHECK(App_LifecycleRxPromote(&rx[d],&rx[d].replay.request)==SYS_ERR_INVALID_ARG);
        CHECK(App_LifecycleRxGetStatus(&rx[d],(AppLifecycleReplayStatus_t *)&rx[d])==SYS_ERR_INVALID_ARG);break;
    case 39:
        m=Request(1U,10U);rx_before=rx[0];CHECK(App_LifecycleRxReceiveRequest(&rx[0],&m,&disposition)==SYS_ERR_INVALID_ARG);
        CHECK(memcmp(&rx_before,&rx[0],sizeof(rx_before))==0);CHECK(App_LifecycleTxStage(&tx[0],&m)==SYS_ERR_INVALID_ARG);break;
    case 40:
        CHECK(App_LifecycleTxStage(&tx[d],&m)==SYS_OK);forced_send=SYS_ERR_TIMEOUT;
        tx_before=tx[d];CHECK(App_LifecycleTxFlush(&tx[d],Send,&d)==SYS_ERR_TIMEOUT);CHECK(memcmp(&tx_before,&tx[d],sizeof(tx_before))==0);
        forced_send=SYS_OK;CHECK(App_LifecycleTxFlush(&tx[d],Send,&d)==SYS_OK);break;
    case 41:
        CHECK(Begin(d,10U)==0U);CHECK(Finish(d,false)==0U);forced_send=SYS_ERR_IO;
        rx_before=rx[d];CHECK(App_LifecycleRxFlushReply(&rx[d],Send,&d)==SYS_ERR_IO);CHECK(memcmp(&rx_before,&rx[d],sizeof(rx_before))==0);break;
    case 42:
        CHECK(Begin(d,10U)==0U);CHECK(Finish(d,true)==0U);n=send_calls;
        CHECK(App_LifecycleRxFlushReply(&rx[d],Send,&d)==SYS_ERR_NOT_FOUND && send_calls==n);break;
    case 43:
        CHECK(App_LifecycleTxStage(&tx[d],&m)==SYS_OK);CHECK(App_LifecycleMakeReply(&m,9U,SYS_OK,&r)==SYS_OK);
        tx_before=tx[d];CHECK(App_LifecycleTxAcceptReply(&tx[d],&r)==SYS_ERR_INVALID_STATE);
        CHECK(memcmp(&tx_before,&tx[d],sizeof(tx_before))==0);break;
    case 44:
        CHECK(App_LifecycleTxStage(&tx[d],&m)==SYS_OK);CHECK(App_LifecycleTxFlush(&tx[d],Send,&d)==SYS_OK);
        CHECK(App_LifecycleMakeReply(&m,9U,SYS_OK,&r)==SYS_OK);r.identity.consumer_generation++;
        tx_before=tx[d];CHECK(App_LifecycleTxAcceptReply(&tx[d],&r)==SYS_ERR_PROTOCOL);
        CHECK(memcmp(&tx_before,&tx[d],sizeof(tx_before))==0);break;
    case 45:
        CHECK(Begin(d,10U)==0U);CHECK(Finish(d,true)==0U);m.identity.request_id=20U;
        CHECK(App_LifecycleRxReceiveRequest(&rx[d],&m,&disposition)==SYS_OK);
        CHECK(App_LifecycleRxPromote(&rx[d],NULL)==SYS_ERR_INVALID_ARG);
        CHECK(rx[d].active_request_id==10U && rx[d].highest_retired_request_id==0U && rx[d].candidate_state==1U);break;
    case 46:
        CHECK(App_LifecycleTxStage(&tx[d],&m)==SYS_OK);r=m;r.identity.session_id++;
        tx_before=tx[d];CHECK(App_LifecycleTxCancelReady(&tx[d],&r)==SYS_ERR_PROTOCOL);
        CHECK(memcmp(&tx_before,&tx[d],sizeof(tx_before))==0);break;
    case 47:
        CHECK(Begin(d,10U)==0U);CHECK(Finish(d,true)==0U);m.identity.request_id=20U;
        CHECK(App_LifecycleRxReceiveRequest(&rx[d],&m,&disposition)==SYS_OK);CHECK(App_LifecycleRxPromote(&rx[d],&out)==SYS_OK);
        rx_before=rx[d];CHECK(App_LifecycleRxPromote(&rx[d],&out)==SYS_ERR_NOT_FOUND);CHECK(memcmp(&rx_before,&rx[d],sizeof(rx_before))==0);break;
    case 48:
        memset(&tx_before,0,sizeof(tx_before));memset(&rx_before,0,sizeof(rx_before));
        CHECK(App_LifecycleTxStage(&tx_before,&m)==SYS_ERR_INVALID_STATE);
        CHECK(App_LifecycleRxReceiveRequest(&rx_before,&m,&disposition)==SYS_ERR_INVALID_STATE);
        CHECK(App_LifecycleTxInit(&tx_before,APP_LIFECYCLE_USB,APP_LIFECYCLE_STORAGE,1U)==SYS_ERR_INVALID_ARG);
        CHECK(App_LifecycleRxInit(&rx_before,APP_LIFECYCLE_CAMERA,APP_LIFECYCLE_USB,2U)==SYS_ERR_INVALID_ARG);break;
    case 49:
        first=33U;tx_before=tx[d];CHECK(App_LifecycleTxReserveIds(&tx[d],0U,&first)==SYS_ERR_INVALID_ARG && first==33U);
        CHECK(memcmp(&tx_before,&tx[d],sizeof(tx_before))==0);break;
    case 50:
        CHECK(Begin(d,10U)==0U);CHECK(Finish(d,true)==0U);m.identity.request_id=20U;
        CHECK(App_LifecycleRxReceiveRequest(&rx[d],&m,&disposition)==SYS_OK);
        rx_before=rx[d];memset(&out,0x5A,sizeof(out));before_message=out;fail_begin=1U;
        CHECK(App_LifecycleRxPromote(&rx[d],&out)==SYS_ERR_IO);
        CHECK(memcmp(&rx_before,&rx[d],sizeof(rx_before))==0 && memcmp(&out,&before_message,sizeof(out))==0);
        fail_begin=0U;CHECK(App_LifecycleRxPromote(&rx[d],&out)==SYS_OK);
        CHECK(rx[d].highest_retired_request_id==10U && rx[d].active_request_id==20U);break;
    case 51:
        CHECK(Begin(d,10U)==0U);CHECK(Finish(d,true)==0U);CHECK(Receive(m.source,&out));
        CHECK(App_LifecycleRxReceiveRequest(&rx[d],&m,&disposition)==SYS_OK);
        rx[d].reply_pending.message.identity.request_id++;
        rx_before=rx[d];n=send_calls;CHECK(App_LifecycleRxFlushReply(&rx[d],Send,&d)==SYS_ERR_PROTOCOL);
        CHECK(send_calls==n && memcmp(&rx_before,&rx[d],sizeof(rx_before))==0);break;
    case 52:
        //单个窗口连续4096次往返，不靠重新初始化回收缓存
        for(n=1U;n<=4096U;n++)CHECK(Exchange(0U,n)==0U);
        CHECK(business_calls==4096U && rx[0].highest_retired_request_id==4095U && rx[0].active_request_id==4096U);
        CHECK(rx[0].candidate_state==0U && tx[0].pending.phase==0U);break;
    case 53:
        //共享Camera收件箱中，USB候选背压不阻挡Storage和反方向回复
        CHECK(Begin(0U,10U)==0U);CHECK(Finish(0U,true)==0U);CHECK(Receive(APP_LIFECYCLE_USB,&out));
        CHECK(Begin(1U,10U)==0U);CHECK(Finish(1U,true)==0U);CHECK(Receive(APP_LIFECYCLE_STORAGE,&out));
        m=Request(0U,10U);CHECK(App_LifecycleRxReceiveRequest(&rx[0],&m,&disposition)==SYS_OK);
        m=Request(2U,5U);CHECK(App_LifecycleTxStage(&tx[2],&m)==SYS_OK);n=2U;
        CHECK(App_LifecycleTxFlush(&tx[2],Send,&n)==SYS_OK);CHECK(Receive(APP_LIFECYCLE_USB,&out));
        CHECK(App_LifecycleMakeReply(&m,9U,SYS_OK,&r)==SYS_OK);
        notify_fails=true;m=Request(0U,20U);owner=RUNTIME_TARGET_USB;
        CHECK(App_LifecycleMessagesSend(APP_LIFECYCLE_CHANNEL_CAMERA,&m)==SYS_OK);
        CHECK(App_LifecycleMessagesSend(APP_LIFECYCLE_CHANNEL_CAMERA,&m)==SYS_OK);
        CHECK(App_LifecycleMessagesSend(APP_LIFECYCLE_CHANNEL_CAMERA,&r)==SYS_OK);
        m=Request(1U,20U);owner=RUNTIME_TARGET_STORAGE;
        CHECK(App_LifecycleMessagesSend(APP_LIFECYCLE_CHANNEL_CAMERA,&m)==SYS_OK);
        for(n=0U;n<4U;n++)
        {
            CHECK(Receive(APP_LIFECYCLE_CAMERA,&out));
            if(out.kind==APP_LIFECYCLE_REPLY)CHECK(App_LifecycleTxAcceptReply(&tx[2],&out)==SYS_OK);
            else CHECK(App_LifecycleRxReceiveRequest(&rx[out.identity.consumer_id-1U],&out,&disposition)==SYS_OK);
        }
        CHECK(tx[2].pending.phase==APP_LIFECYCLE_TX_COMPLETE);
        CHECK(App_LifecycleRxPromote(&rx[0],&out)==SYS_ERR_BUSY);
        CHECK(App_LifecycleRxPromote(&rx[1],&out)==SYS_OK && out.identity.request_id==20U);
        CHECK(uxQueueMessagesWaiting(s_lifecycle_handles[0])==0U);break;
    default: CHECK(false);
    }
    CHECK(g_failure==0U);return 0;
}
