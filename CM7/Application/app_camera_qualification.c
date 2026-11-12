#include "app_camera_qualification.h"
#include <string.h>
#include "app_camera.h"
#include "app_camera_qualification_internal.h"
#include "platform_sync.h"

//拒绝输出覆盖权威对象，保持Getter和分类接口无隐含修改
static bool Qual_Overlap(const void *a, size_t an, const void *b, size_t bn);
//把固定消费者转换为静态槽号，拒绝扩展消费者
static int Qual_Index(uint32_t consumer);
//在读取窗口前检查所属核心，禁止回调重入
static SysResult_t Qual_Enter(AppCameraQualification_t *core);
//确认原Rx窗口与首次交付身份完全一致
static bool Qual_Window(const AppCameraQualification_t *core, uint32_t index);
//冻结实际决策，不在这里调用Observer或发送消息
static void Qual_Decide(AppCameraQualification_t *core, uint32_t index,
    SysResult_t result, uint32_t session, uint32_t flags);
//从原请求和冻结参数重建临时回复
static SysResult_t Qual_Reply(const AppCameraQualification_t *core, uint32_t index,
    AppLifecycleMessage_t *reply);
//核对固定结果及运输记录，不重复执行业务
static SysResult_t Qual_Submit(AppCameraQualification_t *core, uint32_t index);
//检查其他方向的申请、提交、候选及资格
static bool Qual_OtherWork(const AppCameraQualification_t *core, uint32_t index);
//推进当前首次交付的四类操作
static void Qual_Execute(AppCameraQualification_t *core, uint32_t index,
    const AppQualificationResourceOps_t *ops, void *context);

//用地址差判断重叠，避免区间末地址相加溢出
static bool Qual_Overlap(const void *a, size_t an, const void *b, size_t bn)
{
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x <= y ? y - x < an : x - y < bn;
}

//把固定消费者转换为静态槽号
static int Qual_Index(uint32_t consumer)
{
    if (consumer == APP_CONSUMER_USB)
        return 0;
    if (consumer == APP_CONSUMER_STORAGE)
        return 1;
    return -1;
}

//阻止同一核心在资源回调内被再次调用
static SysResult_t Qual_Enter(AppCameraQualification_t *core)
{
    if (core == NULL)
        return SYS_ERR_INVALID_ARG;
    if (core->initialized != 1U)
        return SYS_ERR_INVALID_STATE;
    if (core->api_busy != 0U)
        return SYS_ERR_BUSY;
    core->api_busy = 1U;
    return SYS_OK;
}

//分配非零代次，拒绝耗尽和输出别名以免回退计数
SysResult_t App_QualificationNextGeneration(uint32_t *counter, uint32_t *generation)
{
    if (counter == NULL || generation == NULL || counter == generation)
        return SYS_ERR_INVALID_ARG;
    if (*counter == UINT32_MAX)
        return SYS_ERR_NO_RESOURCE;
    *generation = ++*counter;
    return SYS_OK;
}

//只绑定一次原Rx方向，不创建任务或新消息通道
SysResult_t App_QualificationInit(AppCameraQualification_t *core)
{
    SysResult_t result;
    uint32_t i;
    const unsigned char *bytes = (const unsigned char *)core;
    if (core == NULL)
        return SYS_ERR_INVALID_ARG;
    if (core->initialized != 0U || core->api_busy != 0U)
        return SYS_ERR_ALREADY_EXISTS;

    //首次对象必须完整清零，不能保留伪造资格或从部分初始化重新开始
    for (i = 0U; i < sizeof(*core); ++i)
        if (bytes[i] != 0U)
            return SYS_ERR_INVALID_STATE;

    //按固定方向初始化，异常时不清已绑定窗口来伪装恢复
    result = App_LifecycleRxInit(&core->rx[0], APP_LIFECYCLE_USB,
        APP_LIFECYCLE_CAMERA, APP_CONSUMER_USB);
    if (result != SYS_OK)
        return result;
    result = App_LifecycleRxInit(&core->rx[1], APP_LIFECYCLE_STORAGE,
        APP_LIFECYCLE_CAMERA, APP_CONSUMER_STORAGE);
    if (result == SYS_OK)
        core->initialized = 1U;
    return result;
}

//只做现有Rx分类，业务只由后续Promote首次交付触发
SysResult_t App_QualificationReceive(AppCameraQualification_t *core,
    const AppLifecycleMessage_t *request, uint32_t *disposition)
{
    int index;
    SysResult_t result;
    if (!App_LifecycleValidate(request) || request->kind != APP_LIFECYCLE_REQUEST ||
        request->target != APP_LIFECYCLE_CAMERA || disposition == NULL)
        return SYS_ERR_INVALID_ARG;
    if (core != NULL && (Qual_Overlap(core, sizeof(*core), disposition, sizeof(*disposition)) ||
        Qual_Overlap(core, sizeof(*core), request, sizeof(*request))))
        return SYS_ERR_INVALID_ARG;
    index = Qual_Index(request->identity.consumer_id);
    result = Qual_Enter(core);
    if (result != SYS_OK)
        return result;
    result = App_LifecycleRxReceiveRequest(&core->rx[index], request, disposition);
    core->api_busy = 0U;
    return result;
}

//验证方向、完整事务键和当前窗口，不把request_id单独作为身份
static bool Qual_Window(const AppCameraQualification_t *core, uint32_t index)
{
    const AppQualificationEntry_t *e = &core->entries[index];
    const AppLifecycleRxContext_t *rx = &core->rx[index];
    const AppLifecycleMessage_t *r = &rx->replay.request;
    return rx->initialized == 1U && rx->replay.occupied == 1U &&
        rx->active_request_id == e->work_request && e->work_request != 0U &&
        App_LifecycleValidate(r) && r->kind == APP_LIFECYCLE_REQUEST &&
        r->source == (index == 0U ? APP_LIFECYCLE_USB : APP_LIFECYCLE_STORAGE) &&
        r->target == APP_LIFECYCLE_CAMERA && r->identity.consumer_id == index + 1U &&
        rx->source == r->source && rx->target == r->target &&
        rx->consumer_id == r->identity.consumer_id &&
        r->identity.request_id == e->work_request &&
        r->identity.session_id == e->work_session &&
        r->identity.consumer_generation == e->work_generation &&
        r->operation == e->work_operation && r->related_request_id == e->work_related;
}

//先保留真实结果，再允许尝试固定回复
static void Qual_Decide(AppCameraQualification_t *core, uint32_t index,
    SysResult_t result, uint32_t session, uint32_t flags)
{
    AppQualificationEntry_t *e = &core->entries[index];
    const AppLifecycleMessage_t *r = &core->rx[index].replay.request;
    e->decision.request_id = e->work_request;
    e->decision.reply_session = session;
    e->decision.flags = flags;
    e->decision.result = result;
    e->decision.business_result = r->business_result;
    e->decision.cleanup_result = r->cleanup_result;
    e->decision.submit_error = SYS_OK;
    e->decision.phase = APP_QUAL_DECIDED;
}

//回复原文仍由Rx持有，本地只使用冻结的少量参数
static SysResult_t Qual_Reply(const AppCameraQualification_t *core, uint32_t index,
    AppLifecycleMessage_t *reply)
{
    const AppQualificationEntry_t *e = &core->entries[index];
    SysResult_t result;
    if (!Qual_Window(core, index) || e->decision.request_id != e->work_request)
        return SYS_ERR_PROTOCOL;
    result = App_LifecycleMakeReply(&core->rx[index].replay.request,
        e->decision.reply_session, e->decision.result, reply);
    if (result != SYS_OK)
        return result;
    reply->completion_flags = e->decision.flags;
    reply->business_result = e->decision.business_result;
    reply->cleanup_result = e->decision.cleanup_result;
    return App_LifecycleMatchesReply(&core->rx[index].replay.request, reply) ?
        SYS_OK : SYS_ERR_PROTOCOL;
}

//在任务边界复核窗口和缓存，只固定同一个已应用决策
static SysResult_t Qual_Submit(AppCameraQualification_t *core, uint32_t index)
{
    AppQualificationEntry_t *e = &core->entries[index];
    AppLifecycleRxContext_t *rx = &core->rx[index];
    AppLifecycleReplayStatus_t status;
    AppLifecycleMessage_t reply, cached;
    SysResult_t result;

    //BLOCKED不自动恢复；BUSY只保留DECIDED等待合法边界复核
    if (e->decision.phase == APP_QUAL_BLOCKED)
        return e->decision.submit_error;
    result = App_LifecycleRxGetStatus(rx, &status);
    if (result == SYS_OK)
        result = Qual_Reply(core, index, &reply);
    if (result != SYS_OK)
    {
        e->decision.submit_error = result;
        if (result != SYS_ERR_BUSY)
            e->decision.phase = APP_QUAL_BLOCKED;
        return result;
    }

    //已固定时不再次Complete，避免把ALREADY_EXISTS误当待发记录修复
    if (status.completed == 0U)
    {
        result = App_LifecycleRxComplete(rx, &reply);
        if (result == SYS_ERR_BUSY)
        {
            e->decision.submit_error = result;
            return result;
        }
    }
    else
        result = SYS_ERR_ALREADY_EXISTS;

    //无论返回码如何，都核对真实固定内容和待发送状态
    if (App_LifecycleReplayGetReply(&rx->replay, &cached) != SYS_OK ||
        memcmp(&reply, &cached, sizeof(reply)) != 0 ||
        !((rx->reply_pending.phase == APP_LIFECYCLE_TX_READY &&
        memcmp(&rx->reply_pending.message, &cached, sizeof(cached)) == 0) ||
        (rx->final_reply_enqueued == 1U && rx->reply_pending.phase == APP_LIFECYCLE_TX_EMPTY)))
    {
        e->decision.phase = APP_QUAL_BLOCKED;
        e->decision.submit_error = result == SYS_OK || result == SYS_ERR_ALREADY_EXISTS ?
            SYS_ERR_PROTOCOL : result;
        return e->decision.submit_error;
    }

    //固定成功只结束业务提交记录，不代表回复已被消费者处理
    memset(&e->decision, 0, sizeof(e->decision));
    e->work_request = 0U;
    return SYS_OK;
}

//排除本笔STOP/QUERY，检查其他资格、候选和未稳定回复
static bool Qual_OtherWork(const AppCameraQualification_t *core, uint32_t index)
{
    uint32_t i;
    for (i = 0U; i < 2U; ++i)
    {
        const AppLifecycleRxContext_t *rx = &core->rx[i];
        if (core->entries[i].phase != APP_QUAL_INACTIVE ||
            (i != index && core->entries[i].work_request != 0U) ||
            core->entries[i].decision.phase != APP_QUAL_NONE ||
            rx->candidate_state != APP_LIFECYCLE_CANDIDATE_EMPTY ||
            (i != index && rx->replay.occupied != 0U &&
            (rx->replay.completed == 0U || rx->final_reply_enqueued == 0U ||
            rx->reply_pending.phase != APP_LIFECYCLE_TX_EMPTY)))
            return true;
    }
    return false;
}

//推进四种操作，真实归还证据缺失时保持等待
static void Qual_Execute(AppCameraQualification_t *core, uint32_t index,
    const AppQualificationResourceOps_t *ops, void *context)
{
    AppQualificationEntry_t *e = &core->entries[index];
    const AppLifecycleMessage_t *r = &core->rx[index].replay.request;
    uint32_t state = ops->state(context);
    uint32_t generation = r->identity.consumer_generation;
    SysResult_t result;
    AppLifecycleIdentity_t grant;

    //未知资源状态不能被当成未启动或可以复用
    if (state > APP_RESOURCE_FAULT_DRAINING)
    {
        Qual_Decide(core, index, SYS_ERR_PROTOCOL, r->identity.session_id, 0U);
        return;
    }

    //QUERY只分配会话，不调用硬件准备或启动
    if (r->operation == APP_LIFECYCLE_SESSION_QUERY)
    {
        if (state == APP_RESOURCE_PREPARING || state == APP_RESOURCE_STOPPING ||
            state == APP_RESOURCE_FAULT_DRAINING)
            result = SYS_ERR_BUSY;
        else if (core->session_id == 0U || core->session_closed != 0U)
        {
            if (Qual_OtherWork(core, index) ||
                (core->session_id != 0U && (ops->idle == NULL || ops->idle(context) != SYS_OK)))
                result = SYS_ERR_BUSY;
            else if (core->session_id == UINT32_MAX)
                result = SYS_ERR_NO_RESOURCE;
            else
            {
                ++core->session_id;
                core->session_closed = 0U;
                result = SYS_OK;
            }
        }
        else
            result = SYS_OK;
        Qual_Decide(core, index, result, result == SYS_OK ? core->session_id : 0U, 0U);
        return;
    }

    //旧会话不能改变当前资格，也不能消耗当前会话的申请代次
    if (r->identity.session_id != core->session_id || core->session_id == 0U || core->session_closed != 0U)
    {
        Qual_Decide(core, index, SYS_ERR_INVALID_STATE, r->identity.session_id, 0U);
        return;
    }

    if (r->operation == APP_LIFECYCLE_ACQUIRE)
    {
        //更高合法尝试消耗代次，但不覆盖仍有效grant的退出身份
        if (generation <= e->highest_seen_generation)
            result = e->phase == APP_QUAL_ACTIVE || e->phase == APP_QUAL_RELEASING ?
                SYS_ERR_BUSY : SYS_ERR_INVALID_STATE;
        else
        {
            e->highest_seen_generation = generation;
            if (e->phase != APP_QUAL_INACTIVE || state == APP_RESOURCE_PREPARING ||
                state == APP_RESOURCE_STOPPING || state == APP_RESOURCE_FAULT_DRAINING)
                result = SYS_ERR_BUSY;
            else
            {
                e->phase = APP_QUAL_ACQUIRING;
                result = state == APP_RESOURCE_RUNNING ? SYS_OK : ops->start(context);
                if (result == SYS_OK && ops->state(context) != APP_RESOURCE_RUNNING)
                    result = SYS_ERR_INVALID_STATE;
                e->phase = result == SYS_OK ? APP_QUAL_ACTIVE : APP_QUAL_INACTIVE;
                if (result == SYS_OK)
                {
                    e->grant_session = core->session_id;
                    e->grant_generation = generation;
                    e->grant_request = e->work_request;
                }
            }
        }
        Qual_Decide(core, index, result, core->session_id, 0U);
        return;
    }

    if (r->operation == APP_LIFECYCLE_RELEASE)
    {
        //先精确匹配旧grant，不能用最高已见代次拒绝其安全退出
        if ((e->phase != APP_QUAL_ACTIVE && e->phase != APP_QUAL_RELEASING) ||
            e->grant_session != r->identity.session_id || e->grant_generation != generation)
        {
            Qual_Decide(core, index, SYS_ERR_INVALID_STATE, core->session_id, 0U);
            return;
        }
        e->phase = APP_QUAL_RELEASING;
        grant = r->identity;
        grant.request_id = e->grant_request;
        result = ops->released == NULL ? SYS_ERR_NOT_SUPPORTED : ops->released(context, &grant);
        if (result != SYS_OK)
        {
            e->decision.submit_error = result;
            return;
        }

        //证据确认后撤销一次资格，不调用Media Release，也不宣称文件成功
        e->phase = APP_QUAL_INACTIVE;
        e->grant_session = 0U;
        e->grant_generation = 0U;
        e->grant_request = 0U;
        Qual_Decide(core, index, SYS_OK, core->session_id, APP_LIFECYCLE_QUALIFICATION_RELEASED);
        return;
    }

    //STOP_IF_IDLE不代替RELEASE；其他消费者仍活动时返回终结BUSY
    if (generation < e->highest_seen_generation || r->operation != APP_LIFECYCLE_STOP_IF_IDLE)
    {
        Qual_Decide(core, index, SYS_ERR_INVALID_STATE, core->session_id, 0U);
        return;
    }
    if (Qual_OtherWork(core, index) || ops->idle == NULL || ops->idle(context) != SYS_OK)
    {
        Qual_Decide(core, index, SYS_ERR_BUSY, core->session_id, 0U);
        return;
    }

    //从未启动不执行假Stop，已确认停止只表达本次会话的历史事实
    if (state == APP_RESOURCE_UNSTARTED || state == APP_RESOURCE_STOPPED_CONFIRMED)
        result = SYS_OK;
    else
        result = ops->stop(context);
    if (result == SYS_ERR_BUSY)
        return;
    state = ops->state(context);
    if (result == SYS_OK && state != APP_RESOURCE_UNSTARTED && state != APP_RESOURCE_STOPPED_CONFIRMED)
        result = SYS_ERR_INVALID_STATE;
    if (result == SYS_OK)
        core->session_closed = 1U;
    Qual_Decide(core, index, result, core->session_id,
        result == SYS_OK && state == APP_RESOURCE_STOPPED_CONFIRMED ? APP_LIFECYCLE_HARDWARE_STOPPED : 0U);
}

//每方向最多一笔，不因一方等待证据或固定失败阻塞另一方
SysResult_t App_QualificationProcess(AppCameraQualification_t *core,
    const AppQualificationResourceOps_t *ops, void *context)
{
    uint32_t i;
    AppLifecycleMessage_t request;
    SysResult_t result, first = SYS_OK;
    if (ops == NULL || ops->state == NULL || ops->start == NULL || ops->stop == NULL)
        return SYS_ERR_INVALID_ARG;
    result = Qual_Enter(core);
    if (result != SYS_OK)
        return result;
    for (i = 0U; i < 2U; ++i)
    {
        AppQualificationEntry_t *e = &core->entries[i];
        if (e->decision.phase == APP_QUAL_NONE && e->work_request == 0U)
        {
            result = App_LifecycleRxPromote(&core->rx[i], &request);
            if (result == SYS_OK)
            {
                e->work_request = request.identity.request_id;
                e->work_session = request.identity.session_id;
                e->work_generation = request.identity.consumer_generation;
                e->work_operation = request.operation;
                e->work_related = request.related_request_id;
            }
            else if (result != SYS_ERR_BUSY && result != SYS_ERR_NOT_FOUND && first == SYS_OK)
                first = result;
        }

        //提交窗口异常时不执行业务，更不能根据未知状态撤销资格
        if (e->work_request != 0U && e->decision.phase == APP_QUAL_NONE)
        {
            if (Qual_Window(core, i))
                Qual_Execute(core, i, ops, context);
            else
            {
                e->decision.phase = APP_QUAL_BLOCKED;
                e->decision.submit_error = SYS_ERR_PROTOCOL;
            }
        }
        if (e->decision.phase != APP_QUAL_NONE)
        {
            result = Qual_Submit(core, i);
            if (result != SYS_OK && first == SYS_OK)
                first = result;
        }
    }
    core->api_busy = 0U;
    return first;
}

//只重试现有回复运输，禁止伪造请求修复Pending缺失
SysResult_t App_QualificationFlush(AppCameraQualification_t *core, uint32_t consumer,
    AppLifecycleSendFn send, void *context)
{
    int index = Qual_Index(consumer);
    AppLifecycleRxContext_t *rx;
    SysResult_t result;
    if (index < 0 || send == NULL)
        return SYS_ERR_INVALID_ARG;
    result = Qual_Enter(core);
    if (result != SYS_OK)
        return result;
    rx = &core->rx[index];
    if (core->entries[index].decision.phase != APP_QUAL_NONE)
        result = SYS_ERR_BUSY;
    else if (rx->final_reply_enqueued != 0U && rx->reply_pending.phase == APP_LIFECYCLE_TX_EMPTY)
        result = SYS_OK;
    else
        result = App_LifecycleRxFlushReply(rx, send, context);

    //异常固定后无待发回复或正文不一致，保留为明确BLOCKED
    if ((result == SYS_ERR_NOT_FOUND && rx->replay.completed != 0U && rx->final_reply_enqueued == 0U) ||
        result == SYS_ERR_PROTOCOL)
    {
        core->entries[index].decision.phase = APP_QUAL_BLOCKED;
        core->entries[index].decision.submit_error = result;
    }
    core->api_busy = 0U;
    return result;
}

//只复制语义字段，不采样资源、提交决策或发送消息
SysResult_t App_QualificationGetStatus(const AppCameraQualification_t *core,
    uint32_t consumer, AppQualificationStatus_t *status)
{
    int index = Qual_Index(consumer);
    const AppQualificationEntry_t *e;
    if (core == NULL || status == NULL || index < 0 ||
        Qual_Overlap(core, sizeof(*core), status, sizeof(*status)))
        return SYS_ERR_INVALID_ARG;
    if (core->initialized != 1U)
        return SYS_ERR_INVALID_STATE;
    if (core->api_busy != 0U)
        return SYS_ERR_BUSY;
    e = &core->entries[index];
    status->phase = e->phase;
    status->session_id = core->session_id;
    status->grant_generation = e->grant_generation;
    status->highest_seen_generation = e->highest_seen_generation;
    status->decision_phase = e->decision.phase;
    status->submit_error = e->decision.submit_error;
    return SYS_OK;
}

//在协议适配边界验证所属任务，资源模块不增加RTOS依赖
static SysResult_t Qual_AppAccess(AppCameraQualification_t **core,
    const AppQualificationResourceOps_t **ops, bool preparing)
{
    if (Platform_SyncGetException() != 0U || !App_MessagesIsOwner(RUNTIME_TARGET_CAMERA))
        return SYS_ERR_INVALID_STATE;
    return App_CameraQualificationAccess(core, ops, preparing);
}

//显式准备App_Camera私有工具，当前业务任务不调用此入口
SysResult_t App_CameraQualificationPrepare(void)
{
    AppCameraQualification_t *core;
    const AppQualificationResourceOps_t *ops;
    SysResult_t result = Qual_AppAccess(&core, &ops, true);
    return result == SYS_OK ? App_QualificationInit(core) : result;
}

//所属任务把原协议消息交给唯一资格对象
SysResult_t App_CameraQualificationReceive(const AppLifecycleMessage_t *request, uint32_t *disposition)
{
    AppCameraQualification_t *core;
    const AppQualificationResourceOps_t *ops;
    SysResult_t result = Qual_AppAccess(&core, &ops, false);
    return result == SYS_OK ? App_QualificationReceive(core, request, disposition) : result;
}

//在所属调度边界推进，不隐藏Media或Bridge处理
SysResult_t App_CameraQualificationProcess(void)
{
    AppCameraQualification_t *core;
    const AppQualificationResourceOps_t *ops;
    SysResult_t result = Qual_AppAccess(&core, &ops, false);
    return result == SYS_OK ? App_QualificationProcess(core, ops, NULL) : result;
}

//只发送原Rx已经固定的回复，不创建新运输通道
SysResult_t App_CameraQualificationFlush(uint32_t consumer, AppLifecycleSendFn send, void *context)
{
    AppCameraQualification_t *core;
    const AppQualificationResourceOps_t *ops;
    SysResult_t result = Qual_AppAccess(&core, &ops, false);
    return result == SYS_OK ? App_QualificationFlush(core, consumer, send, context) : result;
}

//只读资格副本，不能作为帧释放证明
SysResult_t App_CameraQualificationGetStatus(uint32_t consumer, AppQualificationStatus_t *status)
{
    AppCameraQualification_t *core;
    const AppQualificationResourceOps_t *ops;
    SysResult_t result = Qual_AppAccess(&core, &ops, false);
    return result == SYS_OK ? App_QualificationGetStatus(core, consumer, status) : result;
}
