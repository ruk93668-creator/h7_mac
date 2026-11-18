// 协调 Camera 配置、启停、保护性停止和真实退出证明。

#include "camera_module.h"
#include <string.h>
#define MAGIC 0x56434134U
// 校验所有者调用并设置重入保护，调用方仍负责跨任务串行化。
static SysResult_t enter(VCamera *camera)
{
    if (!camera || camera->magic != MAGIC || !camera->ops->is_owner(camera->port))
        return SYS_ERR_INVALID_STATE;
    if (camera->busy)
        return SYS_ERR_BUSY;
    camera->busy = 1;
    return 0;
}
// 退出当前调用的重入保护。
static SysResult_t leave(VCamera *camera, SysResult_t result)
{
    camera->busy = 0;
    return result;
}
// 将本轮观察到的故障记录到模块上下文。
static void fault(VCamera *camera, SysResult_t result)
{
    if (result && result != SYS_ERR_BUSY && result != SYS_ERR_NO_RESOURCE && !camera->first_fault)
        camera->first_fault = result;
}
// 将底层返回结果整理为 Camera 当前操作的本地结果。
static int local(uint32_t op) { return op == LC_CONFIGURE || op == LC_CLEANUP; }
// 查询后端可用于安全推进的资源事实。
static int safe(uint32_t flags)
{
    return (flags & VC_STOP_MASK) == VC_STOP_MASK || (flags & VC_COLD_MASK) == VC_COLD_MASK;
}
// 查询后端可用于安全推进的资源事实。
static const MRSource *source(VCamera *camera, uint32_t backend)
{
    return backend == CORE_BACKEND_RAW ? &camera->resources.raw_source : &camera->resources.jpeg_source;
}
// 校验状态修改操作运行在约定的所有者上下文。
static int lc_owner(void *port_context)
{
    VCamera *camera = port_context;
    return camera->ops->is_owner(camera->port);
}
// 从 Camera 端口读取真实格式和采集能力。
static SysResult_t capabilities(VCamera *camera, const CoreMediaFormat *format, VCameraCapabilities *out)
{
    VCameraCapabilities requested_caps;
    SysResult_t result;
    memset(&requested_caps, 0, sizeof(requested_caps));
    result = VCamera_Translate(format, &requested_caps.native);
    if (result)
        return result;
    result = MRSource_Query(source(camera, format->backend), &requested_caps.resource);
    if (result)
        return result;
    if (requested_caps.resource.backend != format->backend || format->max_spans > requested_caps.resource.spans)
        return SYS_ERR_NOT_SUPPORTED;
    result = camera->ops->validate_layout(camera->port, format, &requested_caps.native, &requested_caps.resource);
    if (result)
        return result;
    *out = requested_caps;
    return 0;
}
// 检查当前模块事实是否允许受理指定生命周期过渡。
static SysResult_t can_begin(void *port_context, const LCRequest *request)
{
    VCamera *camera = port_context;
    VCameraCapabilities cap;
    SysResult_t result;
    if (camera->work)
        return SYS_ERR_BUSY;
    if (!camera->bound)
        return SYS_ERR_INVALID_STATE;
    if (request->id.operation == LC_ACTIVATE && !camera->prepared)
        return SYS_ERR_INVALID_STATE;
    if (request->id.operation == LC_CONFIGURE || request->id.operation == LC_ACTIVATE)
    {
        result = capabilities(camera, &camera->format, &cap);
        if (result)
            return result;
    }
    return camera->ops->gate(camera->port, request, VC_GATE_BEGIN);
}
// 从生命周期过渡请求登记本模块操作，真实完成由事实检查决定。
static SysResult_t begin(void *port_context, const LCRequest *request)
{
    VCamera *camera = port_context;
    camera->operation = *request;
    camera->work = 1;
    camera->submitted = 0;
    camera->decided = 0;
    camera->delivered = 0;
    memset(&camera->result, 0, sizeof(camera->result));
    camera->result.id = request->id;
    return 0;
}
// 检查端口和资源事实是否满足当前过渡的完成条件。
static SysResult_t prove(void *port_context, const LCRequest *request, uint32_t fact)
{
    VCamera *camera = port_context;
    VCameraObservation observation;
    SysResult_t result;
    if (!camera->work && fact == request->expected)
        return camera->ops->gate(camera->port, request, VC_GATE_PROVE);
    if (!LC_IdEqual(&request->id, &camera->operation.id))
        return SYS_ERR_PROTOCOL;
    result = camera->ops->gate(camera->port, request, VC_GATE_PROVE);
    if (result)
        return result;
    if (request->id.operation == LC_CONFIGURE)
        return 0;
    if (request->id.operation == LC_ACTIVATE)
    {
        result = camera->ops->facts(camera->port, &observation);
        if (result)
            return result;
        return ((camera->result.observation.flags & observation.flags) & (VC_DEVICE_RUNNING | VC_MEDIA_RUNNING)) ==
                       (VC_DEVICE_RUNNING | VC_MEDIA_RUNNING)
                   ? 0
                   : SYS_ERR_BUSY;
    }
    if (request->id.operation == LC_CLEANUP)
    {
        result = camera->ops->facts(camera->port, &observation);
        if (result)
            return result;
        return safe(observation.flags) ? 0 : SYS_ERR_BUSY;
    }
    result = camera->ops->facts(camera->port, &observation);
    if (result)
        return result;
    return safe(camera->result.observation.flags) && safe(observation.flags) ? 0 : SYS_ERR_BUSY;
}
const struct V2LifecycleOps VCamera_LifecycleOps = {lc_owner, can_begin, begin, prove};
// Camera：绑定模块所有者及端口并初始化本地上下文。
SysResult_t VCamera_Init(VCamera *camera, uint32_t module, uint32_t session, const VCameraOps *ops,
                         void *port, const VCameraResources *resources)
{
    SysResult_t result;
    if (!camera || camera->magic || !ops || !resources || !ops->is_owner || !ops->validate_layout ||
        !ops->gate || !ops->request || !ops->read_result || !ops->consume_result || !ops->facts ||
        !ops->release_identity || !ops->begin_round || !ops->process || !ops->returns ||
        !ops->finish_round)
        return SYS_ERR_INVALID_ARG;
    if (!ops->is_owner(port))
        return SYS_ERR_INVALID_STATE;
    memset(camera, 0, sizeof(*camera));
    camera->ops = ops;
    camera->port = port;
    camera->resources = *resources;
    result = LC_Init(&camera->lifecycle, module, session, &VCamera_LifecycleOps, camera);
    if (result)
        return result;
    camera->magic = MAGIC;
    return 0;
}
// Camera：查询模块端口公开的格式及运行能力。
SysResult_t VCamera_QueryCapabilities(VCamera *camera, const CoreMediaFormat *format,
                                      VCameraCapabilities *out)
{
    SysResult_t result;
    if (!format || !out || !camera || V2_Overlap(camera, sizeof(*camera), out, sizeof(*out)))
        return SYS_ERR_INVALID_ARG;
    result = enter(camera);
    if (result)
        return result;
    result = capabilities(camera, format, out);
    return leave(camera, result);
}
// Camera：校验并绑定已选配置候选，保留底层应用配置的独立结果。
SysResult_t VCamera_BindCandidate(VCamera *camera, const V2Registry *registry, const V2CompositionCandidate *candidate)
{
    const V2ModuleDesc *module;
    uint32_t module_index;
    VCameraCapabilities cap;
    SysResult_t result;
    if (!registry || !candidate || candidate->candidate_only != 1 || !candidate->media.pipeline_mask || !candidate->media.entity_mask)
        return SYS_ERR_INVALID_ARG;
    result = enter(camera);
    if (result)
        return result;
    module = V2Registry_Get(registry, (V2ModuleId){camera->lifecycle.module});
    if (!module || !(candidate->modules.selected_mask & (1U << (module - registry->modules))))
        return leave(camera, SYS_ERR_NOT_FOUND);
    if (!candidate->modules.count || candidate->modules.count > V2_MODULE_MAX)
        return leave(camera, SYS_ERR_PROTOCOL);
    for (module_index = 0; module_index < candidate->modules.count; module_index++)
        if (candidate->modules.order[module_index].value == camera->lifecycle.module)
            break;
    if (module_index == candidate->modules.count)
        return leave(camera, SYS_ERR_PROTOCOL);
    if (camera->bound)
        return leave(camera, !memcmp(&camera->format, &candidate->media.format, sizeof(camera->format))
                            ? SYS_OK
                            : SYS_ERR_NOT_SUPPORTED);
    if (camera->lifecycle.state != LC_UNCONFIGURED || camera->lifecycle.pending || camera->work)
        return leave(camera, SYS_ERR_BUSY);
    result = capabilities(camera, &candidate->media.format, &cap);
    if (result)
        return leave(camera, result);
    camera->format = candidate->media.format;
    camera->native = cap.native;
    camera->bound = 1;
    return leave(camera, 0);
}
// Camera：登记带执行身份的生命周期请求，实际完成由模块轮次推进。
SysResult_t VCamera_Request(VCamera *camera, const LCRequest *request, uint32_t now)
{
    SysResult_t result = enter(camera);
    if (result)
        return result;
    if (camera->round != 1)
        return leave(camera, SYS_ERR_INVALID_STATE);
    result = LC_Request(&camera->lifecycle, request, now);
    return leave(camera, result);
}
// 建立 Camera 本轮的停止推进和操作记账边界。
SysResult_t VCamera_BeginRound(VCamera *camera)
{
    SysResult_t result = enter(camera);
    if (result)
        return result;
    if (camera->round)
        return leave(camera, SYS_ERR_INVALID_STATE);
    camera->ops->begin_round(camera->port);
    camera->round = 1;
    return leave(camera, 0);
}
// 推进本轮 Camera 配置、启动及采集端口操作。
SysResult_t VCamera_Process(VCamera *camera, uint32_t now)
{
    SysResult_t result = enter(camera);
    if (result)
        return result;
    if (camera->round != 1)
        return leave(camera, SYS_ERR_INVALID_STATE);
    LC_Process(&camera->lifecycle, now);
    if (camera->work && !local(camera->operation.id.operation) && !camera->submitted && !camera->decided)
    {
        {
            uint32_t state;
            SysResult_t observed;
            LC_Status(&camera->lifecycle, &state, &observed);
            if (observed == SYS_ERR_TIMEOUT)
                result = SYS_ERR_TIMEOUT;
            else
                result = camera->ops->request(camera->port, &camera->operation, &camera->native);
        }
        if (!result)
            camera->submitted = 1;
        else if (result != SYS_ERR_BUSY && result != SYS_ERR_NO_RESOURCE)
        {
            camera->result.result = result;
            camera->decided = 1;
            fault(camera, result);
        }
    }
    camera->ops->process(camera->port);
    camera->round = 2;
    return leave(camera, 0);
}
// 推进 Camera 所有者负责的帧归还。
SysResult_t VCamera_Returns(VCamera *camera)
{
    SysResult_t result = enter(camera);
    if (result)
        return result;
    if (camera->round != 2)
        return leave(camera, SYS_ERR_INVALID_STATE);
    camera->ops->returns(camera->port);
    camera->round = 3;
    return leave(camera, 0);
}
// 汇集本轮观察到的 Camera 端口结果供后续完成判定使用。
static void collect(VCamera *camera, uint32_t now)
{
    SysResult_t result;
    VCameraResult native_result;
    LCCompletion done;
    uint32_t fact;
    if (!camera->work)
        return;
    if (local(camera->operation.id.operation) && !camera->decided)
        camera->decided = 1;
    if (camera->submitted && !camera->decided)
    {
        result = camera->ops->read_result(camera->port, &camera->operation.id, &native_result);
        if (!result)
        {
            if (!LC_IdEqual(&native_result.id, &camera->operation.id) || native_result.result > 0)
            {
                camera->port_error = SYS_ERR_PROTOCOL;
                fault(camera, camera->port_error);
                return;
            }
            camera->result = native_result;
            camera->decided = 1;
            fault(camera, native_result.result);
        }
        else if (result != SYS_ERR_NOT_FOUND && result != SYS_ERR_BUSY && result != SYS_ERR_NO_RESOURCE)
        {
            camera->port_error = result;
            fault(camera, result);
            return;
        }
    }
    if (camera->decided && !camera->delivered)
    {
        fact = camera->result.result ? LC_ERROR : LC_Target(camera->operation.id.operation);
        result = LC_Complete(&camera->lifecycle, &camera->operation.id, camera->result.result, fact);
        if (!result)
            camera->delivered = 1;
        else
        {
            camera->port_error = result;
            fault(camera, result);
        }
    }
    LC_Process(&camera->lifecycle, now);
    if (LC_Read(&camera->lifecycle, &camera->operation.id, &done))
        return;
    fault(camera, done.result);
    if (camera->submitted)
    {
        result = camera->ops->consume_result(camera->port, &camera->result);
        if (result)
        {
            camera->port_error = result;
            fault(camera, result);
            return;
        }
    }
    if (done.actual_result == SYS_OK)
    {
        if (done.fact == LC_INACTIVE && camera->operation.id.operation == LC_CONFIGURE)
            camera->prepared = 1;
        if (done.fact == LC_UNCONFIGURED)
            camera->prepared = 0;
    }
    camera->work = 0;
}
// 在本轮归还推进后处理停止并核对完成事实。
SysResult_t VCamera_FinishRound(VCamera *camera, uint32_t now)
{
    SysResult_t result = enter(camera);
    if (result)
        return result;
    if (camera->round != 3)
        return leave(camera, SYS_ERR_INVALID_STATE);
    camera->ops->finish_round(camera->port);
    collect(camera, now);
    {
        VCameraObservation observation;
        if (camera->ops->facts(camera->port, &observation) == 0)
        {
            fault(camera, observation.media_result);
            if (observation.stop_result && !camera->first_stop_failure.stop_result)
            {
                camera->first_stop_failure = observation;
                fault(camera, observation.stop_result);
            }
        }
    }
    camera->round = 0;
    return leave(camera, 0);
}
// Camera：按身份读取模块已固定的完成结果。
SysResult_t VCamera_ReadCompletion(VCamera *camera, const V2ExecId *id, LCCompletion *out)
{
    SysResult_t result;
    if (!camera || !out || V2_Overlap(camera, sizeof(*camera), out, sizeof(*out)))
        return SYS_ERR_INVALID_ARG;
    result = enter(camera);
    if (result)
        return result;
    result = LC_Read(&camera->lifecycle, id, out);
    return leave(camera, result);
}
// Camera：确认与请求身份匹配的完成已消费。
SysResult_t VCamera_ConfirmCompletion(VCamera *camera, const LCCompletion *completion)
{
    SysResult_t result = enter(camera);
    if (result)
        return result;
    if (camera->work)
        return leave(camera, SYS_ERR_BUSY);
    result = LC_Confirm(&camera->lifecycle, completion);
    return leave(camera, result);
}
// Camera：读取模块的生命周期状态和观察到的业务事实。
SysResult_t VCamera_QueryStatus(VCamera *camera, VCameraStatus *out)
{
    VCameraStatus status;
    SysResult_t result;
    if (!camera || !out || V2_Overlap(camera, sizeof(*camera), out, sizeof(*out)))
        return SYS_ERR_INVALID_ARG;
    result = enter(camera);
    if (result)
        return result;
    result = LC_Status(&camera->lifecycle, &status.lifecycle, &status.operation_error);
    if (result)
        return leave(camera, result);
    result = camera->ops->facts(camera->port, &status.current);
    if (result)
        return leave(camera, result);
    status.first_stop_failure = camera->first_stop_failure;
    status.bound = camera->bound;
    status.prepared = camera->prepared;
    status.first_fault = camera->first_fault;
    status.port_error = camera->port_error;
    *out = status;
    return leave(camera, 0);
}
// Camera：分别读取退出所需的硬件、业务和媒体资源证据。
SysResult_t VCamera_QueryExitEvidence(VCamera *camera, const MRRelease *release, VCameraExitEvidence *out)
{
    VCameraExitEvidence evidence;
    MRContext *resource;
    SysResult_t result;
    if (!camera || !release || !out || V2_Overlap(camera, sizeof(*camera), out, sizeof(*out)))
        return SYS_ERR_INVALID_ARG;
    result = enter(camera);
    if (result)
        return result;
    if (!camera->bound)
        return leave(camera, SYS_ERR_INVALID_STATE);
    memset(&evidence, 0, sizeof(evidence));
    evidence.release = *release;
    evidence.backend = camera->format.backend;
    evidence.identity_result = camera->ops->release_identity(camera->port, release);
    resource = camera->format.backend == CORE_BACKEND_RAW ? camera->resources.raw_resource
                                                     : camera->resources.jpeg_resource;
    evidence.resource_result = evidence.identity_result
                            ? evidence.identity_result
                            : (!resource ? SYS_ERR_NOT_SUPPORTED : MR_VerifyExit(resource, release));
    *out = evidence;
    result = evidence.resource_result;
    return leave(camera, result);
}
