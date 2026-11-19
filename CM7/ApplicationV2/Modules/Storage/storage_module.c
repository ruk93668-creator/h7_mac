// 协调 Storage 生命周期、文件操作和实际退出证据。

#include "storage_module.h"
#include <string.h>

#define MAGIC 0x53544F36U
#define FILE_NEW 0U
#define FILE_MOUNTED 1U
#define FILE_WRITING 2U
#define FILE_FINISH 3U
#define FILE_CLOSE 4U
#define FILE_UNMOUNT 5U
#define FILE_DONE 6U

// 校验状态修改操作运行在约定的所有者上下文。
static int owner(void *port_context)
{
    VStorage *storage = port_context;
    return storage && storage->ports.consumer->is_owner(storage->ports.consumer_port);
}

// 校验所有者调用并设置重入保护，调用方仍负责跨任务串行化。
static SysResult_t enter(VStorage *storage)
{
    if (!storage || storage->magic != MAGIC || !owner(storage))
        return SYS_ERR_INVALID_STATE;
    if (storage->busy)
        return SYS_ERR_BUSY;
    storage->busy = 1U;
    return SYS_OK;
}

// 退出当前调用的重入保护。
static SysResult_t leave(VStorage *storage, SysResult_t result)
{
    storage->busy = 0U;
    return result;
}

// 识别仍需等待端口或运输推进的结果。
static int waiting(SysResult_t result)
{
    return result == SYS_ERR_BUSY || result == SYS_ERR_NO_RESOURCE || result == SYS_ERR_NOT_FOUND;
}

// 检查本地数据路径是否已完成排空。
static int quiet(const VStorage *storage)
{
    return (storage->facts.flags & VST_IO_QUIET) != 0U;
}

// 检查文件和卷操作是否已完成收尾。
static int volume_clear(const VStorage *storage)
{
    return quiet(storage) && (storage->facts.flags & VST_VOLUME_RELEASED) &&
           !(storage->facts.flags & (VST_FILE_OPEN | VST_VOLUME_HELD));
}

// 独立核验模块退出所需的各项事实。
static int exited(const VStorage *storage)
{
    return quiet(storage) && storage->frame.state == VSF_FREE && !storage->consumer.grant.session &&
           (storage->consumer.phase == VSC_IDLE || storage->consumer.phase == VSC_IDLE_REQUEST);
}

// 检查旧操作及资源清理结果是否允许接收配置。
static int ready_to_configure(const VStorage *storage)
{
    return !storage->drain && exited(storage) && storage->consumer.phase == VSC_IDLE && volume_clear(storage);
}

// 按发生顺序选择应保留的错误结果。
static SysResult_t first_result(const VStorage *storage)
{
    return storage->business_error ? storage->business_error : storage->cleanup_error;
}

// 记录首个错误，后续恢复结果不能覆盖它。
static void remember(VStorage *storage, SysResult_t result, SysResult_t *slot)
{
    if (!result)
        return;
    if (!*slot)
        *slot = result;
    if (!storage->first_fault)
        storage->first_fault = result;
}

// 检查当前模块事实是否允许受理指定生命周期过渡。
static SysResult_t can_begin(void *port_context, const LCRequest *request)
{
    VStorage *storage = port_context;
    if (request->id.operation == LC_CONFIGURE && !storage->bound)
        return SYS_ERR_INVALID_STATE;
    if (request->id.operation == LC_DEACTIVATE || request->id.operation == LC_ERROR_PROCESS)
        return SYS_OK;
    return ready_to_configure(storage) ? SYS_OK : SYS_ERR_BUSY;
}

// 从生命周期过渡请求登记本模块操作，真实完成由事实检查决定。
static SysResult_t begin(void *port_context, const LCRequest *request)
{
    VStorage *storage = port_context;
    SysResult_t result;
    if (request->id.operation == LC_ACTIVATE)
    {
        //先向 Camera 申请资格，后续按授权事实推进文件操作。
        result = VStorageConsumer_Begin(&storage->consumer);
        if (result)
            return result;
        storage->file_step = FILE_NEW;
        storage->frames = 0U;
        storage->business_error = 0;
        storage->cleanup_error = 0;
        storage->recovery_result = 0;
    }
    if (request->id.operation == LC_ERROR_PROCESS)
    {
        storage->recovery_issued = 0U;
        storage->recovery_result = 0;
    }
    if (request->id.operation == LC_DEACTIVATE)
        storage->drain = 1U;
    storage->work = 1U;
    return SYS_OK;
}

// 检查端口和资源事实是否满足当前过渡的完成条件。
static SysResult_t prove(void *port_context, const LCRequest *request, uint32_t fact)
{
    VStorage *storage = port_context;
    if (request->expected == fact)
        return SYS_OK;
    if (fact == LC_ACTIVE)
        return storage->consumer.phase == VSC_GRANTED && quiet(storage) &&
                       (storage->facts.flags & (VST_VOLUME_HELD | VST_FILE_OPEN)) ==
                           (VST_VOLUME_HELD | VST_FILE_OPEN)
                   ? SYS_OK
                   : SYS_ERR_BUSY;
    if (request->id.operation == LC_DEACTIVATE)
        return exited(storage) && volume_clear(storage) ? SYS_OK : SYS_ERR_BUSY;
    return ready_to_configure(storage) ? SYS_OK : SYS_ERR_BUSY;
}

const struct V2LifecycleOps VStorage_LifecycleOps = {owner, can_begin, begin, prove};

// Storage：绑定模块所有者及端口并初始化本地上下文。
SysResult_t VStorage_Init(VStorage *storage, uint32_t module, uint32_t session, const VStorageCaps *caps,
                          const VStoragePorts *ports)
{
    const VStorageBackendOps *backend_ops;
    const VStorageFrameOps *frame_ops;
    SysResult_t result;
    if (!storage || storage->magic || !module || !session || !caps || !caps->max_payload || !ports)
        return SYS_ERR_INVALID_ARG;
    backend_ops = ports->backend;
    frame_ops = ports->frame;
    if (!backend_ops || !backend_ops->mount || !backend_ops->open || !backend_ops->write || !backend_ops->finish || !backend_ops->close || !backend_ops->unmount ||
        !backend_ops->recover || !backend_ops->observe || !frame_ops || !frame_ops->post || !frame_ops->withdraw || !frame_ops->offer ||
        !frame_ops->return_frame || !frame_ops->receipt || !frame_ops->consume || !frame_ops->retired)
        return SYS_ERR_INVALID_ARG;
    result = VStorageConsumer_Init(&storage->consumer, ports->consumer, ports->consumer_port);
    if (result)
        return result;
    storage->ports = *ports;
    storage->caps = *caps;
    result = LC_Init(&storage->lifecycle, module, session, &VStorage_LifecycleOps, storage);
    if (result)
    {
        memset(storage, 0, sizeof(*storage)); //初始化还未获取外部资源。
        return result;
    }
    storage->magic = MAGIC;
    return SYS_OK;
}

// Storage：校验并绑定已选配置候选，保留底层应用配置的独立结果。
SysResult_t VStorage_BindCandidate(VStorage *storage, const V2Registry *registry,
                                   const V2CompositionCandidate *candidate, const VStorageConfig *cfg)
{
    const V2ModuleDesc *module;
    uint32_t interval;
    uint16_t selection_index;
    SysResult_t result = enter(storage);
    if (result)
        return result;
    if (!registry || !candidate || candidate->candidate_only != 1U || !candidate->media.pipeline_mask ||
        !candidate->media.entity_mask || !candidate->modules.count || candidate->modules.count > V2_MODULE_MAX)
        return leave(storage, SYS_ERR_INVALID_ARG);
    module = V2Registry_Get(registry, (V2ModuleId){storage->lifecycle.module});
    if (!module || !(candidate->modules.selected_mask & (1UL << (module - registry->modules))))
        return leave(storage, SYS_ERR_NOT_FOUND);
    for (selection_index = 0; selection_index < candidate->modules.count; ++selection_index)
        if (candidate->modules.order[selection_index].value == storage->lifecycle.module)
            break;
    if (selection_index == candidate->modules.count)
        return leave(storage, SYS_ERR_PROTOCOL);
    if (!cfg || !cfg->sink_entity || !candidate->media.graph || !candidate->media.sink_count ||
        candidate->media.sink_count > GRAPH_SINK_MAX)
        return leave(storage, SYS_ERR_INVALID_ARG);
    for (selection_index = 0; selection_index < candidate->media.sink_count; ++selection_index)
        if (candidate->media.sinks[selection_index].sink.value == cfg->sink_entity)
            break;
    if (selection_index == candidate->media.sink_count)
        return leave(storage, SYS_ERR_NOT_FOUND);
    if (cfg->mode == VST_AVI && (cfg->sampling.num != candidate->media.sinks[selection_index].rate.num ||
                                 cfg->sampling.den != candidate->media.sinks[selection_index].rate.den))
        return leave(storage, SYS_ERR_INVALID_ARG);
    //配置候选仍须通过真实存储能力验证。
    result = VStorage_ValidateFormat(&candidate->media.format, cfg, &storage->caps, &interval);
    if (result)
        return leave(storage, result);
    if (storage->bound)
        return leave(storage, !memcmp(cfg, &storage->config, sizeof(*cfg)) &&
                                !memcmp(&candidate->media.format, &storage->format, sizeof(storage->format))
                            ? SYS_OK
                            : SYS_ERR_NOT_SUPPORTED);
    if (storage->lifecycle.state != LC_UNCONFIGURED || storage->lifecycle.pending || storage->drain)
        return leave(storage, SYS_ERR_BUSY);
    storage->config = *cfg;
    storage->format = candidate->media.format;
    storage->interval_ms = interval;
    storage->bound = 1U;
    return leave(storage, SYS_OK);
}

// Storage：查询模块端口公开的格式及运行能力。
SysResult_t VStorage_QueryCapabilities(VStorage *storage, const CoreMediaFormat *format,
                                       const VStorageConfig *cfg, uint32_t *ms)
{
    SysResult_t result = enter(storage);
    if (result)
        return result;
    if (!ms || V2_Overlap(storage, sizeof(*storage), ms, sizeof(*ms)))
        return leave(storage, SYS_ERR_INVALID_ARG);
    return leave(storage, VStorage_ValidateFormat(format, cfg, &storage->caps, ms));
}

// Storage：登记带执行身份的生命周期请求，实际完成由模块轮次推进。
SysResult_t VStorage_Request(VStorage *storage, const LCRequest *request, uint32_t now)
{
    SysResult_t result = enter(storage);
    return result ? result : leave(storage, LC_Request(&storage->lifecycle, request, now));
}

// 提交模块的真实操作完成结果。
static void complete(VStorage *storage, SysResult_t result, uint32_t fact)
{
    if (!LC_Complete(&storage->lifecycle, &storage->lifecycle.request.id, result, fact))
        storage->work = 0U;
}

// 刷新本轮可观察的底层运行和退出事实。
static void poll_facts(VStorage *storage)
{
    VStorageFacts fresh = {0};
    SysResult_t result = storage->ports.backend->observe(storage->ports.backend_port, &fresh);
    storage->facts.flags = result ? 0U : fresh.flags; //查询失败后禁止复用旧静止证据。
    if (result && !waiting(result))
        remember(storage, result, &storage->cleanup_error);
}

// 按已受理配置推进模块启动，每轮依据端口结果继续。
static void start(VStorage *storage)
{
    const VStorageBackendOps *backend_ops = storage->ports.backend;
    void *port_context = storage->ports.backend_port;
    SysResult_t result = SYS_OK;

    if (storage->consumer.phase == VSC_IDLE && storage->consumer.result)
    {
        remember(storage, storage->consumer.result, &storage->business_error);
        complete(storage, storage->consumer.result, LC_INACTIVE);
        return;
    }
    if (storage->consumer.phase == VSC_BLOCKED)
    {
        remember(storage, storage->consumer.result, &storage->business_error);
        complete(storage, storage->consumer.result, LC_ERROR);
        return;
    }
    if (storage->consumer.phase != VSC_GRANTED)
        return;
    if (storage->lifecycle.first_error == SYS_ERR_TIMEOUT)
        result = SYS_ERR_TIMEOUT;
    else if (storage->file_step == FILE_NEW)
    {
        //取得资格后再访问卷。
        result = backend_ops->mount(port_context, &storage->config);
        if (!result)
            storage->file_step = FILE_MOUNTED;
    }
    else if (storage->file_step == FILE_MOUNTED)
    {
        //卷准备成功后创建文件，不提前开始录像。
        result = backend_ops->open(port_context, &storage->config, &storage->format);
        if (!result)
            storage->file_step = FILE_WRITING;
    }
    else if (!prove(storage, &storage->lifecycle.request, LC_ACTIVE))
        complete(storage, SYS_OK, LC_ACTIVE);

    if (result)
    {
        remember(storage, result, &storage->business_error);
        storage->drain = 1U;
        complete(storage, result, LC_ERROR);
    }
}

// 写入当前帧的数据跨度，保留实际写入结果。
static void write_frame(VStorage *storage, uint32_t now)
{
    const VStorageOffer *offer = &storage->frame.current;
    SysResult_t result;

    //已准入的录像帧可排空。发生文件错误后仍归还，但不再写。
    if (!storage->business_error && (storage->facts.flags & VST_FILE_OPEN) &&
        (!storage->drain || storage->config.mode == VST_AVI))
    {
        //用本次 Offer 的真实帧视图写入存储。
        result = storage->ports.backend->write(storage->ports.backend_port, &storage->config, &storage->format, &offer->view,
                                    offer->timestamp);
        if (result)
        {
            remember(storage, result, &storage->business_error);
            storage->drain = 1U;
        }
        else
        {
            storage->frames++;
            storage->sample_tick = now;
            if (storage->config.mode == VST_SNAPSHOT)
                storage->drain = 1U;
        }
        storage->facts.flags &= ~VST_IO_QUIET; //写入后的归还需要重新观测。
    }
    storage->frame.state = VSF_RETURN;
}

// 推进文件封尾及关闭，文件完成不证明媒体租约已归还。
static void finish_files(VStorage *storage)
{
    const VStorageBackendOps *backend_ops = storage->ports.backend;
    void *port_context = storage->ports.backend_port;
    SysResult_t result = SYS_OK;
    if (storage->file_step < FILE_FINISH)
        storage->file_step = FILE_FINISH;
    switch (storage->file_step)
    {
    case FILE_FINISH:
        if ((storage->facts.flags & VST_FILE_OPEN) && !storage->business_error)
        {
            if (storage->frames)
                //先完成文件格式封尾，再继续关闭和卸载。
                result = backend_ops->finish(port_context);
            else if (storage->config.mode == VST_AVI)
                result = SYS_ERR_NOT_FOUND; //空AVI不能宣称完整成片。
        }
        storage->file_step = FILE_CLOSE;
        break;
    case FILE_CLOSE:
        if (storage->facts.flags & VST_FILE_OPEN)
            //等待真实关闭结果，不能用业务 STOP 代替文件关闭。
            result = backend_ops->close(port_context);
        storage->file_step = FILE_UNMOUNT;
        break;
    case FILE_UNMOUNT:
        if (storage->facts.flags & VST_VOLUME_HELD)
            result = (storage->facts.flags & VST_FILE_OPEN) ? SYS_ERR_INVALID_STATE : backend_ops->unmount(port_context);
        storage->file_step = FILE_DONE;
        break;
    default:
        break;
    }
    remember(storage, result, &storage->cleanup_error);
}

// 提取 Storage 端口已观测到的退出条件。
static uint32_t release_flags(const VStorage *storage)
{
    uint32_t flags =
        APP_LIFECYCLE_INPUT_CLOSED | APP_LIFECYCLE_WORK_QUIESCENT | APP_LIFECYCLE_FRAMES_DRAINED;
    if (storage->facts.flags & VST_FILE_FINISHED)
        flags |= APP_LIFECYCLE_FILE_FINISHED;
    if (storage->facts.flags & VST_FILE_CLOSED)
        flags |= APP_LIFECYCLE_FILE_CLOSED;
    if (storage->facts.flags & VST_VOLUME_RELEASED)
        flags |= APP_LIFECYCLE_VOLUME_RELEASED;
    return flags;
}

// 推进模块停止，等待底层和资源证据齐备。
static void stop(VStorage *storage)
{
    SysResult_t result;
    if (storage->frame.state != VSF_FREE || !quiet(storage))
        return;
    if (storage->file_step != FILE_DONE)
    {
        finish_files(storage);
        return;
    }
    if (storage->consumer.phase == VSC_GRANTED)
    {
        //在满足排空门禁后请求资格释放，携带真实业务和收尾错误。
        result = VStorageConsumer_End(&storage->consumer, storage->business_error, storage->cleanup_error,
                                 release_flags(storage));
        remember(storage, result, &storage->cleanup_error);
        return;
    }
    if (exited(storage))
    {
        storage->drain = 0U;
        if (storage->work && storage->lifecycle.request.id.operation == LC_DEACTIVATE)
        {
            result = first_result(storage);
            if (!result && (!volume_clear(storage) ||
                       (storage->frames && (storage->facts.flags & (VST_FILE_FINISHED | VST_FILE_CLOSED)) !=
                                         (VST_FILE_FINISHED | VST_FILE_CLOSED))))
            {
                result = SYS_ERR_INVALID_STATE;
                remember(storage, result, &storage->cleanup_error);
            }
            complete(storage, result, result ? LC_ERROR : LC_INACTIVE);
        }
    }
}

// 按端口契约尝试恢复底层资源，保留原始故障。
static void recover(VStorage *storage)
{
    if (!storage->recovery_issued && !volume_clear(storage))
    {
        //显式恢复可使原来故障的IDMA停止，然后继续归还原Frame。
        //完成的同步写入不会在此轮重新执行。
        storage->recovery_issued = 1U;
        if (storage->facts.flags & VST_FS_FAULT)
            storage->recovery_result = storage->ports.backend->recover(storage->ports.backend_port);
        else
            storage->recovery_result = SYS_ERR_INVALID_STATE;
        if (storage->recovery_result)
            complete(storage, storage->recovery_result, LC_ERROR);
        return;
    }
    if (ready_to_configure(storage))
    {
        storage->bound = 0U;
        complete(storage, SYS_OK, LC_UNCONFIGURED);
    }
}

// 在 Storage 所有者上下文推进生命周期、文件收尾及归还。
SysResult_t VStorage_RunRound(VStorage *storage, uint32_t now)
{
    SysResult_t result = enter(storage);
    uint32_t op, before;
    int accepting;
    if (result)
        return result;
    poll_facts(storage);
    (void)LC_Process(&storage->lifecycle, now);
    op = storage->lifecycle.request.id.operation;
    //继续处理原资格事务，包括停止期间的回复。
    result = VStorageConsumer_Process(&storage->consumer);
    if (storage->consumer.phase == VSC_BLOCKED)
        remember(storage, result, &storage->cleanup_error);
    accepting = !storage->drain && quiet(storage) && !(storage->facts.flags & VST_FS_FAULT) &&
                storage->lifecycle.state == LC_ACTIVE && storage->consumer.phase == VSC_GRANTED &&
                (storage->facts.flags & VST_FILE_OPEN) &&
                (!storage->frames || now - storage->sample_tick >= storage->interval_ms);
    before = storage->frame.state;
    //本地入口关闭后仍要推进帧归还和收据退休。
    result = VStorageFrame_Process(&storage->frame, storage->ports.frame, storage->ports.frame_port, &storage->consumer.grant,
                              accepting, quiet(storage));
    if (storage->frame.state == VSF_BLOCKED ||
        (result && (!waiting(result) || (before == VSF_ACK && storage->frame.state == VSF_CONSUME))))
    {
        remember(storage, result, &storage->business_error);
        storage->drain = 1U;
    }
    if (storage->file_step == FILE_WRITING && (storage->facts.flags & VST_FS_FAULT))
    {
        remember(storage, SYS_ERR_IO, &storage->business_error);
        storage->drain = 1U;
    }
    if (storage->frame.state == VSF_READY && (quiet(storage) || storage->business_error))
        write_frame(storage, now);
    if (storage->drain)
        stop(storage);
    if (storage->work && op == LC_DEACTIVATE && first_result(storage))
        complete(storage, first_result(storage), LC_ERROR);
    if (storage->work && op == LC_ACTIVATE)
        start(storage);
    if (storage->work && op == LC_ERROR_PROCESS)
        recover(storage);
    if (storage->work && (op == LC_CONFIGURE || op == LC_CLEANUP) && ready_to_configure(storage))
    {
        if (op == LC_CLEANUP)
            storage->bound = 0U;
        complete(storage, SYS_OK, LC_Target(op));
    }
    return leave(storage, SYS_OK);
}

// Storage：读取模块的生命周期状态和观察到的业务事实。
SysResult_t VStorage_QueryStatus(VStorage *storage, VStorageStatus *status)
{
    SysResult_t result = enter(storage);
    if (result)
        return result;
    if (!status || V2_Overlap(storage, sizeof(*storage), status, sizeof(*status)))
        return leave(storage, SYS_ERR_INVALID_ARG);
    status->lifecycle = storage->lifecycle.state;
    status->drain = storage->drain;
    status->frame_state = storage->frame.state;
    status->frames = storage->frames;
    status->qualification = storage->consumer.phase;
    status->grant = storage->consumer.grant;
    status->files = storage->facts;
    status->first_fault = storage->first_fault;
    status->business_error = storage->business_error;
    status->cleanup_error = storage->cleanup_error;
    status->recovery_result = storage->recovery_result;
    status->camera_stop_result = storage->consumer.camera_stop_result;
    return leave(storage, SYS_OK);
}

// Storage：分别读取退出所需的硬件、业务和媒体资源证据。
SysResult_t VStorage_QueryExitEvidence(VStorage *storage, const MRGrant *grant, uint32_t *flags)
{
    SysResult_t result = enter(storage);
    if (result)
        return result;
    if (!grant || !grant->session || !flags || V2_Overlap(storage, sizeof(*storage), flags, sizeof(*flags)) ||
        V2_Overlap(grant, sizeof(*grant), flags, sizeof(*flags)) || !MRGrant_Equal(grant, &storage->consumer.grant))
        return leave(storage, SYS_ERR_PROTOCOL);
    if (!storage->drain || storage->file_step != FILE_DONE || storage->frame.state != VSF_FREE || !quiet(storage))
        return leave(storage, SYS_ERR_BUSY);
    *flags = release_flags(storage);
    return leave(storage, SYS_OK);
}

// Storage：按身份读取模块已固定的完成结果。
SysResult_t VStorage_ReadCompletion(VStorage *storage, const V2ExecId *id, LCCompletion *out)
{
    SysResult_t result = enter(storage);
    if (result)
        return result;
    if (!out || V2_Overlap(storage, sizeof(*storage), out, sizeof(*out)))
        return leave(storage, SYS_ERR_INVALID_ARG);
    return leave(storage, LC_Read(&storage->lifecycle, id, out));
}

// Storage：确认与请求身份匹配的完成已消费。
SysResult_t VStorage_ConfirmCompletion(VStorage *storage, const LCCompletion *completion)
{
    SysResult_t result = enter(storage);
    return result ? result : leave(storage, LC_Confirm(&storage->lifecycle, completion));
}
