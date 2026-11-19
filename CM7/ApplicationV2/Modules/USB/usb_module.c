// 协调 USB 生命周期、UVC 启停和真实退出证明。

#include "usb_module.h"
#include <string.h>

#define MAGIC 0x56554235U
#define STOP_PROOF (VUSB_MODE_STOPPED | VUSB_READERS_QUIET | VUSB_BRIDGE_IDLE)
#define START_PROOF (VUSB_MODE_STARTED | VUSB_CLASS_READY)
#define SEND_PROOF (START_PROOF | VUSB_HOST_CONFIGURED | VUSB_CAN_SUBMIT)

// 校验状态修改操作运行在约定的所有者上下文。
static int owner(void *port_context)
{
    VUSB *usb = port_context;
    return usb && usb->ops->is_owner(usb->port);
}

// 识别仍需等待端口或运输推进的结果。
static int waiting(SysResult_t result)
{
    return result == SYS_ERR_BUSY || result == SYS_ERR_NO_RESOURCE || result == SYS_ERR_NOT_FOUND;
}

// 检查端口证据是否满足调用方要求的事实掩码。
static int proven(const VUSB *usb, uint32_t mask)
{
    return (usb->observation.flags & mask) == mask;
}

// 检查本地数据路径是否已完成排空。
static int local_drained(const VUSB *usb)
{
    return !(usb->observation.flags & VUSB_FRAME_OWNED) &&
           proven(usb, VUSB_READERS_QUIET | VUSB_BRIDGE_IDLE) && VUSBFrame_IsIdle(&usb->frame);
}

// 检查底层停止事实，保持与本地帧排空独立。
static int stopped(const VUSB *usb)
{
    return local_drained(usb) && proven(usb, STOP_PROOF) && !(usb->observation.flags & VUSB_MODE_STARTED);
}

// 独立核验模块退出所需的各项事实。
static int exit_confirmed(const VUSB *usb)
{
    return stopped(usb) && !usb->consumer.grant.session &&
           (usb->consumer.phase == VUC_IDLE || usb->consumer.phase == VUC_IDLE_REQUEST);
}

// 检查退出相关的身份及收据是否已退休。
static int retired(const VUSB *usb)
{
    return exit_confirmed(usb) && !usb->drain && usb->consumer.phase == VUC_IDLE;
}

// 校验所有者调用并设置重入保护，调用方仍负责跨任务串行化。
static SysResult_t enter(VUSB *usb)
{
    if (!usb || usb->magic != MAGIC || !owner(usb))
        return SYS_ERR_INVALID_STATE;
    if (usb->busy)
        return SYS_ERR_BUSY;
    usb->busy = 1;
    return SYS_OK;
}

// 退出当前调用的重入保护。
static SysResult_t leave(VUSB *usb, SysResult_t result)
{
    usb->busy = 0;
    return result;
}

// 记录首个错误，后续恢复结果不能覆盖它。
static void remember(VUSB *usb, SysResult_t result)
{
    if (!result || waiting(result))
        return;
    if (!usb->first_fault)
        usb->first_fault = result;
    if (!usb->active_fault)
        usb->active_fault = result;
}

// 检查当前模块事实是否允许受理指定生命周期过渡。
static SysResult_t can_begin(void *port_context, const LCRequest *request)
{
    VUSB *usb = port_context;
    if (request->id.operation == LC_CONFIGURE && !usb->bound)
        return SYS_ERR_INVALID_STATE;
    if (request->id.operation != LC_DEACTIVATE && !retired(usb))
        return SYS_ERR_BUSY;
    return SYS_OK;
}

// 从生命周期过渡请求登记本模块操作，真实完成由事实检查决定。
static SysResult_t begin(void *port_context, const LCRequest *request)
{
    VUSB *usb = port_context;
    SysResult_t result;
    if (request->id.operation == LC_ACTIVATE)
    {
        if (usb->mode == VU_MODE_UVC)
        {
            //通过资格事务启动 Camera 资源准备。
            result = VUSBConsumer_Begin(&usb->consumer);
            if (result)
                return result;
        }
        usb->frame.first_error = 0; //前一交接已退休，可开始本次诊断。
        usb->start_issued = 0;
    }
    if (request->id.operation == LC_DEACTIVATE)
    {
        usb->drain = 1;
        usb->mode_stop_done = 0;
        usb->stop_error = 0;
    }
    usb->work = 1;
    return SYS_OK;
}

// 检查端口和资源事实是否满足当前过渡的完成条件。
static SysResult_t prove(void *port_context, const LCRequest *request, uint32_t fact)
{
    VUSB *usb = port_context;
    if (fact == request->expected)
        return SYS_OK; //操作未提交，原稳定事实保持。
    if (fact == LC_ACTIVE)
    {
        if (usb->mode == VU_MODE_NONE)
            return SYS_OK;
        if (usb->mode == VU_MODE_TEST)
            return proven(usb, VUSB_MODE_STARTED) ? SYS_OK : SYS_ERR_BUSY;
        return proven(usb, START_PROOF) && usb->consumer.phase == VUC_GRANTED ? SYS_OK : SYS_ERR_BUSY;
    }
    if (request->id.operation == LC_DEACTIVATE)
        return exit_confirmed(usb) ? SYS_OK : SYS_ERR_BUSY;
    return retired(usb) ? SYS_OK : SYS_ERR_BUSY;
}

const struct V2LifecycleOps VUSB_LifecycleOps = {owner, can_begin, begin, prove};

// USB：绑定模块所有者及端口并初始化本地上下文。
SysResult_t VUSB_Init(VUSB *usb, uint32_t module, uint32_t session, const VUSBOps *usb_ops, void *port_context,
                      const VUSBConsumerOps *consumer, void *cp, const VUSBFrameOps *frame,
                      void *fp)
{
    SysResult_t result;
    if (!usb || usb->magic || !module || !session || !usb_ops || !usb_ops->is_owner || !usb_ops->start ||
        !usb_ops->mode_process || !usb_ops->cancel || !usb_ops->uvc_process || !usb_ops->stop_mode || !usb_ops->observe)
        return SYS_ERR_INVALID_ARG;
    if (!usb_ops->is_owner(port_context))
        return SYS_ERR_INVALID_STATE;
    usb->ops = usb_ops;
    usb->port = port_context;
    result = VUSBConsumer_Init(&usb->consumer, consumer, cp);
    if (!result)
        result = VUSBFrame_Init(&usb->frame, frame, fp);
    if (!result)
        result = LC_Init(&usb->lifecycle, module, session, &VUSB_LifecycleOps, usb);
    if (result)
    {
        //这些初始化没有提交外部工作，允许修正端口后重试。
        memset(usb, 0, sizeof(*usb));
        return result;
    }
    usb->magic = MAGIC;
    return SYS_OK;
}

// USB：查询模块端口公开的格式及运行能力。
SysResult_t VUSB_QueryCapabilities(VUSB *usb, const CoreMediaFormat *format,
                                   const CoreFrameRate *rate, VideoStreamConfig_t *out)
{
    SysResult_t result = enter(usb);
    if (result)
        return result;
    if (!out || V2_Overlap(usb, sizeof(*usb), out, sizeof(*out)))
        return leave(usb, SYS_ERR_INVALID_ARG);
    return leave(usb, VUSB_Translate(format, rate, out));
}

// USB：校验并绑定已选配置候选，保留底层应用配置的独立结果。
SysResult_t VUSB_BindCandidate(VUSB *usb, const V2Registry *registry, const V2CompositionCandidate *candidate,
                               uint32_t mode, const CoreFrameRate *rate)
{
    const V2ModuleDesc *module;
    uint16_t module_index;
    VideoStreamConfig_t native = {0};
    SysResult_t result = enter(usb);
    if (result)
        return result;
    if (!registry || !candidate || candidate->candidate_only != 1 || mode > VU_MODE_UVC)
        return leave(usb, SYS_ERR_INVALID_ARG);
    module = V2Registry_Get(registry, (V2ModuleId){usb->lifecycle.module});
    if (!module || !(candidate->modules.selected_mask & (1UL << (module - registry->modules))))
        return leave(usb, SYS_ERR_NOT_FOUND);
    if (!candidate->modules.count || candidate->modules.count > V2_MODULE_MAX)
        return leave(usb, SYS_ERR_PROTOCOL);
    for (module_index = 0; module_index < candidate->modules.count; ++module_index)
        if (candidate->modules.order[module_index].value == usb->lifecycle.module)
            break;
    if (module_index == candidate->modules.count)
        return leave(usb, SYS_ERR_PROTOCOL);
    if (mode == VU_MODE_UVC)
    {
        if (!candidate->media.pipeline_mask || !candidate->media.entity_mask)
            return leave(usb, SYS_ERR_INVALID_ARG);
        result = VUSB_Translate(&candidate->media.format, rate, &native);
        if (result)
            return leave(usb, result);
    }
    if (usb->bound)
        return leave(usb, usb->mode == mode &&
                                !memcmp(&usb->format, &candidate->media.format, sizeof(usb->format)) &&
                                !memcmp(&usb->native, &native, sizeof(native))
                            ? SYS_OK
                            : SYS_ERR_NOT_SUPPORTED);
    if (usb->lifecycle.state != LC_UNCONFIGURED || usb->lifecycle.pending || usb->drain)
        return leave(usb, SYS_ERR_BUSY);
    usb->format = candidate->media.format;
    usb->native = native;
    usb->mode = mode;
    usb->bound = 1;
    return leave(usb, SYS_OK);
}

// USB：登记带执行身份的生命周期请求，实际完成由模块轮次推进。
SysResult_t VUSB_Request(VUSB *usb, const LCRequest *request, uint32_t now)
{
    SysResult_t result = enter(usb);
    return result ? result : leave(usb, LC_Request(&usb->lifecycle, request, now));
}

// 提交模块的真实操作完成结果。
static void complete(VUSB *usb, SysResult_t result, uint32_t fact)
{
    remember(usb, result);
    if (!LC_Complete(&usb->lifecycle, &usb->lifecycle.request.id, result, fact))
        usb->work = 0;
}

//失败后的排空独立于原操作回复生命周期。
static void stop_fault(VUSB *usb, SysResult_t result)
{
    if (!result || waiting(result))
        return;
    remember(usb, result);
    if (!usb->stop_error)
        usb->stop_error = result;
    if (usb->work && usb->lifecycle.request.id.operation == LC_DEACTIVATE)
        complete(usb, result, LC_ERROR);
}

// 按已受理配置推进模块启动，每轮依据端口结果继续。
static void activate(VUSB *usb)
{
    SysResult_t result;
    if (usb->mode == VU_MODE_UVC && usb->consumer.phase != VUC_GRANTED)
    {
        if (usb->consumer.phase == VUC_IDLE && usb->consumer.result)
            complete(usb, usb->consumer.result, LC_INACTIVE);
        return;
    }
    if (usb->mode != VU_MODE_NONE && !usb->start_issued)
    {
        //只有具备前置条件才向 UVC 提交启动意图。
        result = usb->ops->start(usb->port, usb->mode, &usb->native);
        if (waiting(result))
            return;
        usb->start_issued = 1;
        if (result)
        {
            usb->drain = 1;
            usb->mode_stop_done = 0;
            usb->stop_error = result;
            complete(usb, result, LC_ERROR);
        }
        //下一轮观察启动事实，禁止用提交返回值宣告启动成功。
        return;
    }
    if (usb->mode != VU_MODE_NONE && usb->observation.result)
    {
        usb->drain = 1;
        usb->mode_stop_done = 0;
        usb->stop_error = usb->observation.result;
        complete(usb, usb->stop_error, LC_ERROR);
        return;
    }
    if (!prove(usb, &usb->lifecycle.request, LC_ACTIVE))
    {
        complete(usb, SYS_OK, LC_ACTIVE);
        if (!usb->lifecycle.first_error)
            usb->active_fault = SYS_OK;
    }
}

// 刷新本轮可观察的底层运行和退出事实。
static SysResult_t observe(VUSB *usb)
{
    VUSBObservation latest = {0};
    SysResult_t result = usb->ops->observe(usb->port, &latest);
    //观测失败，本轮不能沿用旧的停止或DMA安全证明。
    usb->observation = latest;
    if (result)
    {
        usb->observation.flags = 0;
        usb->observation.result = result;
    }
    return result;
}

// 推进模块停止，等待底层和资源证据齐备。
static void finish_stop(VUSB *usb)
{
    SysResult_t result;
    if (!local_drained(usb))
        return;
    if (!usb->mode_stop_done)
    {
        //从未提交过硬件启动，已有冷态证据时无需调用Stop。
        result = usb->mode == VU_MODE_NONE || (!usb->start_issued && stopped(usb))
                ? SYS_OK
                : usb->ops->stop_mode(usb->port);
        if (result)
        {
            stop_fault(usb, result);
            return;
        }
        usb->mode_stop_done = 1;
        return; //等下一轮重新采样硬件事实。
    }
    if (!stopped(usb))
        return;
    //本地模式和帧交接排空后才进入资格释放步骤。
    if (usb->consumer.phase == VUC_GRANTED)
    {
        stop_fault(usb, VUSBConsumer_End(&usb->consumer, usb->stop_error));
        return;
    }
    if (usb->consumer.phase == VUC_BLOCKED)
        stop_fault(usb, usb->consumer.result);
    //USB可以先完成退出；全局STOP_IF_IDLE仍保留在Tx中，等待原回复。
    if (!exit_confirmed(usb))
        return;
    usb->drain = 0;
    usb->start_issued = 0;
    if (usb->work && usb->lifecycle.request.id.operation == LC_DEACTIVATE)
        complete(usb, SYS_OK, LC_INACTIVE);
}

// 在 USB 所有者上下文推进生命周期、传输及归还。
SysResult_t VUSB_RunRound(VUSB *usb, uint32_t now)
{
    SysResult_t result = enter(usb);
    uint32_t op;
    if (result)
        return result;
    (void)LC_Process(&usb->lifecycle, now);
    //推进原资格事务，后端恢复期间不重开旧身份。
    result = VUSBConsumer_Process(&usb->consumer);
    remember(usb, result);
    if (usb->drain)
        stop_fault(usb, result);
    if (usb->mode != VU_MODE_NONE)
    {
        usb->ops->mode_process(usb->port);
        if (usb->drain && usb->mode == VU_MODE_UVC && !usb->mode_stop_done)
            stop_fault(usb, usb->ops->cancel(usb->port));
        usb->ops->uvc_process(usb->port);
    }
    //观察 UVC 模式与读取状态，用真实事实推进生命周期。
    result = observe(usb);
    remember(usb, result);
    if (usb->drain)
        stop_fault(usb, result);
    if (usb->mode == VU_MODE_UVC)
    {
        //在允许取帧时推进交付，停止时仍推进在途归还。
        result = VUSBFrame_Process(&usb->frame, &usb->consumer.grant, &usb->format,
                              !usb->drain && usb->lifecycle.state == LC_ACTIVE &&
                                  usb->consumer.phase == VUC_GRANTED && proven(usb, SEND_PROOF));
        remember(usb, result);
        remember(usb, usb->frame.first_error);
        if (usb->drain)
        {
            stop_fault(usb, result);
            usb->ops->uvc_process(usb->port);
            stop_fault(usb, observe(usb));
            stop_fault(usb, usb->observation.result);
        }
    }
    if (usb->drain)
        finish_stop(usb);
    op = usb->lifecycle.request.id.operation;
    if (usb->work && op == LC_ACTIVATE)
        activate(usb);
    if (usb->work && (op == LC_CONFIGURE || op == LC_CLEANUP || op == LC_ERROR_PROCESS))
    {
        if (retired(usb))
        {
            if (op != LC_CONFIGURE)
                usb->bound = 0;
            complete(usb, SYS_OK, LC_Target(op));
        }
    }
    //下一轮发布决定，避免在本轮重复尝试 can_begin/begin。
    return leave(usb, SYS_OK);
}

// USB：读取模块的生命周期状态和观察到的业务事实。
SysResult_t VUSB_QueryStatus(VUSB *usb, VUSBStatus *out)
{
    SysResult_t result = enter(usb);
    if (result)
        return result;
    if (!out || V2_Overlap(usb, sizeof(*usb), out, sizeof(*out)))
        return leave(usb, SYS_ERR_INVALID_ARG);
    out->lifecycle = usb->lifecycle.state;
    out->drain = usb->drain;
    out->frame_phase = usb->frame.phase;
    out->consumer_phase = usb->consumer.phase;
    out->grant = usb->consumer.grant;
    out->native = usb->observation;
    out->operation_error = usb->lifecycle.first_error;
    out->first_fault = usb->first_fault;
    out->active_fault = usb->active_fault;
    out->stop_error = usb->stop_error;
    out->camera_stop_result = usb->consumer.camera_stop_result;
    return leave(usb, SYS_OK);
}

// USB：分别读取退出所需的硬件、业务和媒体资源证据。
SysResult_t VUSB_QueryExitEvidence(VUSB *usb, const MRGrant *grant)
{
    SysResult_t result = enter(usb);
    if (result)
        return result;
    if (!grant || !grant->session || !MRGrant_Equal(grant, &usb->consumer.grant))
        return leave(usb, SYS_ERR_PROTOCOL);
    //只是USB本地证据。最终Camera核验仍使用原 Grant + 独立 RELEASE。
    return leave(usb, usb->drain && usb->mode_stop_done && stopped(usb) ? SYS_OK : SYS_ERR_BUSY);
}

// USB：按身份读取模块已固定的完成结果。
SysResult_t VUSB_ReadCompletion(VUSB *usb, const V2ExecId *id, LCCompletion *out)
{
    SysResult_t result = enter(usb);
    if (result)
        return result;
    if (!out || V2_Overlap(usb, sizeof(*usb), out, sizeof(*out)))
        return leave(usb, SYS_ERR_INVALID_ARG);
    return leave(usb, LC_Read(&usb->lifecycle, id, out));
}

// USB：确认与请求身份匹配的完成已消费。
SysResult_t VUSB_ConfirmCompletion(VUSB *usb, const LCCompletion *completion)
{
    SysResult_t result = enter(usb);
    return result ? result : leave(usb, LC_Confirm(&usb->lifecycle, completion));
}
