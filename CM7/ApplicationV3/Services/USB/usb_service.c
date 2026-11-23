#include "usb_service.h"
#include "../../Core/app_messages.h"
#include "../../Tasks/service_tasks.h"
#include "usb_device.h"
#include "usb_uvc.h"
#include "platform_time.h"


typedef struct
{
    AppV3ServiceMailbox_t mailbox;
    const VideoConfig_t *config;
    uint32_t packet_limit;
    bool simulated_running;
    AppV3HardwareReport_t hardware;
    AppV3HardwareRequest_t request;
    SysResult_t rollback_fault;
    bool descriptor_ready;
    bool class_registered;
    bool stop_required;
    bool stop_applied;
    bool waiting_configured;
    bool cancel_requested;
} UsbServiceContext_t;

typedef SysResult_t (*UsbServiceHandler_t)(UsbServiceContext_t *context,
    const AppV3ServiceCommand_t *command);

//只有所属任务修改私有状态。发布后API只读取单独保存的队列入口
static UsbServiceContext_t s_usb;
static AppV3Queue_t s_endpoint;

//只改变模拟状态，保持实际设备状态口独立
static SysResult_t UsbService_Start(UsbServiceContext_t *context,
    const AppV3ServiceCommand_t *command)
{
    (void)command;
    if (context->simulated_running)
        return SYS_ERR_INVALID_STATE;
    context->simulated_running = true;
    return SYS_OK;
}

//模拟停止不会访问硬件
static SysResult_t UsbService_Stop(UsbServiceContext_t *context,
    const AppV3ServiceCommand_t *command)
{
    (void)command;
    if (!context->simulated_running)
        return SYS_ERR_INVALID_STATE;
    context->simulated_running = false;
    return SYS_OK;
}

//校验本服务的模拟参数，失败时保留原值
static SysResult_t UsbService_Parameter(UsbServiceContext_t *context,
    const AppV3ServiceCommand_t *command)
{
    if (command->argument == 0U || command->argument > 1024U)
        return SYS_ERR_INVALID_ARG;
    context->packet_limit = command->argument;
    return SYS_OK;
}

//完成消息携带当前模拟状态，不公开Context地址
static SysResult_t UsbService_Query(UsbServiceContext_t *context,
    const AppV3ServiceCommand_t *command)
{
    (void)context;
    (void)command;
    return SYS_OK;
}

//故障注入只生成软件FAULT
static SysResult_t UsbService_Fail(UsbServiceContext_t *context,
    const AppV3ServiceCommand_t *command)
{
    (void)context;
    (void)command;
    return SYS_ERR_IO;
}

static const UsbServiceHandler_t s_handlers[USB_SERVICE_OP_COUNT] =
{
    [USB_SERVICE_MOCK_START] = UsbService_Start,
    [USB_SERVICE_MOCK_STOP] = UsbService_Stop,
    [USB_SERVICE_SET_PARAMETER] = UsbService_Parameter,
    [USB_SERVICE_QUERY] = UsbService_Query,
    [USB_SERVICE_MOCK_FAIL] = UsbService_Fail
};

#define USB_HW_POLL_MS 10U
#define USB_HW_ENUM_TIMEOUT_MS 5000U
#define USB_HW_STOP_TIMEOUT_MS 1000U
//对应usb_uvc_class.c的UVC_VIDEO_EP_ADDRESS
#define USB_HW_VIDEO_EP 0x81U
#define USB_HW_CONFIGURED 1U
#define USB_HW_VIDEO_QUIET 2U
#define USB_HW_FRAME_HELD 4U

//Class配置在Boot后仅成功一次，重新连接仍复用原描述符
//UsbTask拥有交接端点。它只记录读取权和运输状态，本阶段不调用UVC帧提交。
typedef enum
{
    USB_FRAME_IDLE = 0,
    USB_FRAME_WAIT_OFFER,
    USB_FRAME_CANCEL_PENDING,
    USB_FRAME_CANCEL_SENT,
    USB_FRAME_OFFERED,
    USB_FRAME_READING,
    USB_FRAME_RETURN_PENDING,
    USB_FRAME_WAIT_ACK,
    USB_FRAME_CONSUMED_PENDING
} UsbFrameState_t;

static struct
{
    UsbFrameState_t state;
    uint32_t request_id;
    AppMessage_t transfer;
    SysResult_t result;
} s_frame;

//失联或停止先停止新请求。已经交给本地读者的帧须由读者明确结束读取。
static void UsbFrame_Close(void)
{
    if (s_frame.state == USB_FRAME_WAIT_OFFER)
        s_frame.state = USB_FRAME_CANCEL_PENDING;
    else if (s_frame.state == USB_FRAME_OFFERED)
    {
        s_frame.transfer.frame = (CameraFrame_t){0};
        s_frame.state = USB_FRAME_RETURN_PENDING;
    }
}

static void UsbFrame_Receive(const AppMessage_t *message)
{
    if (message->source != RUNTIME_TARGET_CAMERA || message->command_id != s_frame.request_id)
        return;

    if (message->type == APP_MESSAGE_FRAME_OFFER &&
        (s_frame.state == USB_FRAME_WAIT_OFFER || s_frame.state == USB_FRAME_CANCEL_PENDING ||
         s_frame.state == USB_FRAME_CANCEL_SENT))
    {
        //负向OFFER同样需要消费确认，避免最后一笔取消无法退休。
        if (message->result != SYS_OK && message->stream_epoch == 0U && message->lease_id == 0U)
        {
            s_frame.transfer = *message;
            s_frame.result = message->result;
            s_frame.state = USB_FRAME_CONSUMED_PENDING;
        }
        else if (message->result == SYS_OK && message->stream_epoch != 0U && message->lease_id != 0U)
        {
            bool can_read = s_frame.state == USB_FRAME_WAIT_OFFER &&
                message->frame.data != NULL && message->frame.size_bytes != 0U;
            s_frame.transfer = *message;
            s_frame.state = USB_FRAME_OFFERED;
            if (!can_read)
            {
                s_frame.result = SYS_ERR_INVALID_STATE;
                UsbFrame_Close();
            }
        }
    }
    else if (message->type == APP_MESSAGE_RETURN_ACK && s_frame.state == USB_FRAME_WAIT_ACK &&
        message->stream_epoch == s_frame.transfer.stream_epoch &&
        message->lease_id == s_frame.transfer.lease_id && message->result == SYS_OK)
    {
        if (s_frame.result == SYS_ERR_BUSY)
            s_frame.result = SYS_OK;
        s_frame.state = USB_FRAME_CONSUMED_PENDING;
    }
}

static void UsbFrame_Flush(void)
{
    AppMessage_t message = {.source = RUNTIME_TARGET_USB, .command_id = s_frame.request_id};
    UsbFrameState_t after;

    //队列失败时只保留原身份，本轮不推断已归还或已消费。
    switch (s_frame.state)
    {
        case USB_FRAME_CANCEL_PENDING:
            message.type = APP_MESSAGE_FRAME_CANCEL_REQUEST;
            after = USB_FRAME_CANCEL_SENT;
            break;
        case USB_FRAME_RETURN_PENDING:
            message.type = APP_MESSAGE_FRAME_RETURN;
            after = USB_FRAME_WAIT_ACK;
            break;
        case USB_FRAME_CONSUMED_PENDING:
            message.type = APP_MESSAGE_RETURN_ACK_CONSUMED;
            after = USB_FRAME_IDLE;
            break;
        default:
            return;
    }
    if (s_frame.state != USB_FRAME_CANCEL_PENDING)
    {
        message.stream_epoch = s_frame.transfer.stream_epoch;
        message.lease_id = s_frame.transfer.lease_id;
    }
    if (App_MessagesSend(APP_CHANNEL_TO_CAMERA, &message) == SYS_OK)
    {
        s_frame.state = after;
        if (after == USB_FRAME_IDLE)
            s_frame.transfer = (AppMessage_t){.command_id = 0U};
    }
}

static void UsbFrame_Advance(bool closing)
{
    AppMessage_t message;
    uint32_t count;

    if (closing)
        UsbFrame_Close();
    for (count = 0U; count < 4U && App_MessagesReceive(APP_CHANNEL_TO_USB, &message); ++count)
        UsbFrame_Receive(&message);
    if (closing)
        UsbFrame_Close();
    UsbFrame_Flush();
}

SysResult_t UsbService_RequestFrame(void)
{
    AppMessage_t request = {.type = APP_MESSAGE_FRAME_REQUEST, .source = RUNTIME_TARGET_USB};
    SysResult_t result;

    if (!App_MessagesIsOwner(RUNTIME_TARGET_USB) || s_usb.hardware.state != APP_V3_HW_ACTIVE ||
        (s_usb.hardware.flags & USB_HW_CONFIGURED) == 0U)
        return SYS_ERR_INVALID_STATE;
    if (s_frame.state != USB_FRAME_IDLE)
        return SYS_ERR_BUSY;
    if (s_frame.request_id == UINT32_MAX)
        return SYS_ERR_NO_RESOURCE;

    //发送失败表示请求未受理，下一次仍使用同一个编号。
    request.command_id = s_frame.request_id + 1U;
    result = App_MessagesSend(APP_CHANNEL_TO_CAMERA, &request);
    if (result == SYS_OK)
    {
        s_frame.request_id = request.command_id;
        s_frame.state = USB_FRAME_WAIT_OFFER;
        s_frame.result = SYS_ERR_BUSY;
    }
    return result;
}

SysResult_t UsbService_ReadFrame(CameraFrame_t *frame, uint32_t *frame_id)
{
    if (frame == NULL || frame_id == NULL)
        return SYS_ERR_INVALID_ARG;
    if (!App_MessagesIsOwner(RUNTIME_TARGET_USB) || s_usb.hardware.state != APP_V3_HW_ACTIVE ||
        (s_usb.hardware.flags & USB_HW_CONFIGURED) == 0U)
        return SYS_ERR_INVALID_STATE;
    if (s_frame.state != USB_FRAME_OFFERED)
        return s_frame.state == USB_FRAME_IDLE && s_frame.result != SYS_OK ? s_frame.result : SYS_ERR_BUSY;

    //此后直到读者调用Release，都不能根据STOP、超时或失联推断读者已退出。
    *frame = s_frame.transfer.frame;
    *frame_id = s_frame.transfer.frame_id;
    s_frame.state = USB_FRAME_READING;
    return SYS_OK;
}

SysResult_t UsbService_ReleaseFrame(void)
{
    if (!App_MessagesIsOwner(RUNTIME_TARGET_USB) || s_frame.state != USB_FRAME_READING)
        return SYS_ERR_INVALID_STATE;
    //调用者确认所有读取者已退出。成功只表示提交归还意图。
    s_frame.transfer.frame = (CameraFrame_t){0};
    s_frame.state = USB_FRAME_RETURN_PENDING;
    return SYS_OK;
}

SysResult_t UsbService_CancelFrameRequest(void)
{
    if (!App_MessagesIsOwner(RUNTIME_TARGET_USB) ||
        (s_frame.state != USB_FRAME_WAIT_OFFER && s_frame.state != USB_FRAME_OFFERED))
        return SYS_ERR_INVALID_STATE;
    UsbFrame_Close();
    return SYS_OK;
}

static SysResult_t UsbService_BeginDevice(void)
{
    SysResult_t result;

    if (!s_usb.descriptor_ready)
    {
        VideoStreamConfig_t config = {s_usb.config->camera, s_usb.config->output_fps,
            s_usb.config->output_max_frame_bytes};
        result = USB_UVC_Configure(&config);
        if (result != SYS_OK)
            return result;
        s_usb.descriptor_ready = true;
    }
    //Register和Start均可能留下待STOP的端点或FIFO配置，调用前由USB Service登记清理义务
    s_usb.stop_required = true;
    s_usb.stop_applied = false;

    if (!s_usb.class_registered)
    {
        result = USB_UVC_Register();
        if (result != SYS_OK)
            return result;
        s_usb.class_registered = true;
    }
    result = USB_Device_Start();
    if (result == SYS_OK)
        s_usb.waiting_configured = true;
    return result;
}

//已有Class事实优先；INVALID_STATE只说明没有活动Class，不能单独证明STOP
static SysResult_t UsbService_CheckFrame(void)
{
    UsbUvcStreamStatus_t stream;
    SysResult_t result = USB_UVC_GetStreamStatus(&stream);

    if (result == SYS_ERR_INVALID_STATE)
    {
        //上一轮已观察到持有时，丢失Class查询不能当成归还
        return (s_usb.hardware.flags & USB_HW_FRAME_HELD) != 0U ? SYS_ERR_BUSY : SYS_OK;
    }
    if (result != SYS_OK)
        return result;
    s_usb.hardware.primary = stream.completed_frames;
    if (!stream.frame_owned)
    {
        s_usb.hardware.flags &= (uint16_t)~USB_HW_FRAME_HELD;
        return SYS_OK;
    }

    //当前设计没有任何SubmitFrame调用，这条路径只用于保守处理底层异常
    s_usb.hardware.flags |= USB_HW_FRAME_HELD;
    if (s_usb.cancel_requested && stream.result != SYS_OK && stream.result != SYS_ERR_BUSY)
        return stream.result;
    if (!s_usb.cancel_requested)
    {
        result = USB_UVC_CancelFrame();
        if (result != SYS_OK)
            return result;
        s_usb.cancel_requested = true;
    }
    USB_UVC_Process();
    return SYS_ERR_BUSY;
}

//先确认无源帧，再执行Pcd STOP及Class DeInit，并检查视频端点已经失效
static SysResult_t UsbService_StopDevice(void)
{
    UsbDeviceEndpointState_t endpoint;
    SysResult_t result;

    //既有UVC停止仍独立执行；持有或未确认的交接不能提前宣告READY。
    if (s_frame.state != USB_FRAME_IDLE)
        return SYS_ERR_BUSY;
    if (!s_usb.stop_required)
        return SYS_OK;
    if (!s_usb.stop_applied)
    {
        result = UsbService_CheckFrame();
        if (result != SYS_OK)
            return result;
        result = USB_Device_Stop();
        if (result != SYS_OK)
            return result;
        s_usb.stop_applied = true;
    }
    result = USB_Device_GetEpState(USB_HW_VIDEO_EP, &endpoint);
    if (result != SYS_OK)
        return result;
    if (USB_Device_IsConfigured() || endpoint.opened || endpoint.busy)
        return SYS_ERR_BUSY;
    s_usb.stop_required = false;
    s_usb.stop_applied = false;
    s_usb.waiting_configured = false;
    s_usb.cancel_requested = false;
    s_usb.hardware.flags = USB_HW_VIDEO_QUIET;
    return SYS_OK;
}

//真实START、STOP和RECOVER只登记当前命令，QUERY返回实际状态快照
static SysResult_t UsbService_HardwareCommand(const AppV3ServiceCommand_t *command, SysResult_t result)
{
    AppV3HardwareState_t state = s_usb.hardware.state;

    if (result != SYS_OK || command->operation == USB_SERVICE_HARDWARE_QUERY)
        return AppV3Mailbox_CompleteHardware(&s_usb.mailbox, command, result, &s_usb.hardware);
    if (command->operation == USB_SERVICE_START)
    {
        if (state != APP_V3_HW_READY)
            result = state == APP_V3_HW_ACTIVE ? SYS_ERR_ALREADY_EXISTS : SYS_ERR_INVALID_STATE;
        else
            s_usb.hardware = (AppV3HardwareReport_t){.state = APP_V3_HW_STARTING};
    }
    else if (state == APP_V3_HW_ACTIVE || state == APP_V3_HW_FAULT)
        s_usb.hardware.state = APP_V3_HW_QUIESCING;
    else
        return AppV3Mailbox_CompleteHardware(&s_usb.mailbox, command,
            state == APP_V3_HW_READY ? SYS_OK : SYS_ERR_INVALID_STATE, &s_usb.hardware);

    if (result != SYS_OK)
        return AppV3Mailbox_CompleteHardware(&s_usb.mailbox, command, result, &s_usb.hardware);
    s_usb.rollback_fault = SYS_OK;
    //显式恢复允许重新推进取消，但未证明的源帧持有标志仍保留
    s_usb.cancel_requested = false;
    s_usb.request = (AppV3HardwareRequest_t){*command, Platform_TimeGetMs(), true};
    return SYS_OK;
}

static void UsbService_FinishHardware(SysResult_t result, AppV3HardwareState_t state)
{
    if (result != SYS_OK && s_usb.hardware.first_fault == SYS_OK)
        s_usb.hardware.first_fault = result;
    s_usb.hardware.state = state;
    s_usb.request.pending = false;
    (void)AppV3Mailbox_CompleteHardware(&s_usb.mailbox, &s_usb.request.command, result, &s_usb.hardware);
}

#ifdef APP_V3_USB_FAULT_TRACE_ENABLED
volatile AppV3UsbFaultTrace_t g_app_v3_usb_first_fault;
static SysResult_t s_usb_observed_core_result;
static UsbDeviceR2Trace_t s_usb_platform_trace;
#endif

//仅ACTIVE允许健康挂起；START仍须观察到真实CONFIGURED
static SysResult_t UsbService_ObserveDevice(bool *suspended)
{
    SysResult_t result;
    UsbDeviceLinkState_t link;
    bool configured;
#ifdef APP_V3_USB_FAULT_TRACE_ENABLED
    configured = USB_Device_ReadR2Trace(&s_usb_platform_trace);
    link = (UsbDeviceLinkState_t){s_usb_platform_trace.dev_state, s_usb_platform_trace.dev_old_state,
        s_usb_platform_trace.started, s_usb_platform_trace.stop_required, s_usb_platform_trace.last_result};
#else
    configured = USB_Device_ReadLinkState(&link);
#endif
    if (configured)
        s_usb.hardware.flags |= USB_HW_CONFIGURED;
    else
        s_usb.hardware.flags &= (uint16_t)~USB_HW_CONFIGURED;

    //挂起保留USB所有权和现有持有标志，暂停UVC推进直到主机恢复配置
    if (suspended != NULL)
        *suspended = !configured && link.dev_state == USBD_STATE_SUSPENDED &&
            link.dev_old_state == USBD_STATE_CONFIGURED && link.started &&
            !link.stop_required && link.last_result == SYS_OK;
    if (suspended == NULL || !*suspended)
        USB_UVC_Process();
    result = USB_Device_GetLastResult();
#ifdef APP_V3_USB_FAULT_TRACE_ENABLED
    //保留同一轮读取的Core原值，避免随后Class查询覆盖诊断
    s_usb_observed_core_result = result;
#endif
    //采样时已有设备级错误时保留它，后续成功不能掩盖
    if (suspended != NULL && result == SYS_OK)
        result = link.last_result;
    if (result == SYS_OK && configured)
    {
        UsbUvcStreamStatus_t stream;
        result = USB_UVC_GetStreamStatus(&stream);
        if (result == SYS_ERR_INVALID_STATE)
            return SYS_ERR_BUSY;
        if (result == SYS_OK)
        {
            s_usb.hardware.primary = stream.completed_frames;
            if (stream.frame_owned)
            {
                //本阶段没有帧提交入口，意外占用必须冻结并走明确的停止或恢复
                s_usb.hardware.flags |= USB_HW_FRAME_HELD;
                return SYS_ERR_INVALID_STATE;
            }
            s_usb.hardware.flags &= (uint16_t)~USB_HW_FRAME_HELD;
        }
    }
    return result;
}

//仅由UsbTask推进枚举、失联和有界STOP，绝不提交视频帧
uint32_t AppV3_UsbAdvance(void)
{
    uint32_t now;
    SysResult_t result;

    if (!App_MessagesIsOwner(RUNTIME_TARGET_USB))
        return APP_EVENTS_WAIT_FOREVER;
    if (s_usb.hardware.state == APP_V3_HW_ACTIVE)
    {
        bool suspended;
        result = UsbService_ObserveDevice(&suspended);
        if (((s_usb.hardware.flags & USB_HW_CONFIGURED) == 0U && (!suspended || result != SYS_OK)) ||
            (result != SYS_OK && result != SYS_ERR_BUSY))
        {
#ifdef APP_V3_USB_FAULT_TRACE_ENABLED
            //在STOP清理Core结果前冻结首轮ACTIVE故障，最后提交快照有效标志
            if (g_app_v3_usb_first_fault.captured == 0U)
            {
                g_app_v3_usb_first_fault.time_ms = Platform_TimeGetMs();
                g_app_v3_usb_first_fault.configured = (s_usb.hardware.flags & USB_HW_CONFIGURED) != 0U;
                g_app_v3_usb_first_fault.core_result = s_usb_observed_core_result;
                g_app_v3_usb_first_fault.observe_result = result;
                g_app_v3_usb_first_fault.platform = s_usb_platform_trace;
                g_app_v3_usb_first_fault.captured = 1U;
            }
#endif
            s_usb.hardware.first_fault = result == SYS_OK || result == SYS_ERR_BUSY ? SYS_ERR_IO : result;
            s_usb.hardware.state = APP_V3_HW_FAULT;
        }
    }

    //停止或异常只关闭申请，待发Return和消费确认在FAULT中继续排空。
    UsbFrame_Advance(s_usb.hardware.state != APP_V3_HW_ACTIVE);
    if (s_usb.hardware.state == APP_V3_HW_ACTIVE)
        return USB_HW_POLL_MS;
    if (!s_usb.request.pending)
        return s_frame.state != USB_FRAME_IDLE ? USB_HW_POLL_MS : APP_EVENTS_WAIT_FOREVER;

    now = Platform_TimeGetMs();
    if (s_usb.hardware.state == APP_V3_HW_STARTING)
    {
        result = now - s_usb.request.started_ms >= USB_HW_ENUM_TIMEOUT_MS ? SYS_ERR_TIMEOUT :
            (s_usb.waiting_configured ? UsbService_ObserveDevice(NULL) : UsbService_BeginDevice());
        if (result == SYS_OK && (s_usb.hardware.flags & USB_HW_CONFIGURED) != 0U)
            UsbService_FinishHardware(SYS_OK, APP_V3_HW_ACTIVE);
        if (result != SYS_OK && result != SYS_ERR_BUSY)
        {
            s_usb.rollback_fault = result;
            s_usb.hardware.first_fault = result;
            s_usb.hardware.state = APP_V3_HW_QUIESCING;
            s_usb.request.started_ms = now;
        }
        return USB_HW_POLL_MS;
    }

    result = UsbService_StopDevice();
    if (result == SYS_ERR_BUSY && now - s_usb.request.started_ms < USB_HW_STOP_TIMEOUT_MS)
        return USB_HW_POLL_MS;
    if (result == SYS_ERR_BUSY)
        result = SYS_ERR_TIMEOUT;
    UsbService_FinishHardware(s_usb.rollback_fault != SYS_OK ? s_usb.rollback_fault : result,
        result == SYS_OK ? APP_V3_HW_READY : APP_V3_HW_FAULT);
    return USB_HW_POLL_MS;
}

//一次可读回调只接收一条命令，Event Loop负责控制批次预算
static SysResult_t UsbService_Readable(AppV3Queue_t queue, void *argument)
{
    UsbServiceContext_t *context = &s_usb;
    AppV3ServiceCommand_t command = {0};
    SysResult_t result;
    (void)argument;

    //所属任务与队列都要匹配，禁止跨Service取消息
    if (!App_MessagesIsOwner(RUNTIME_TARGET_USB) ||
        queue.index != context->mailbox.queue.index ||
        queue.generation != context->mailbox.queue.generation)
        return SYS_ERR_INVALID_STATE;

    if (context->request.pending)
        return SYS_ERR_BUSY;
    result = AppV3Mailbox_Receive(&context->mailbox, &command);
    if (command.command_id == 0U || (uint32_t)command.requester >= RUNTIME_TARGET_COUNT)
        return result;

    if (command.operation >= USB_SERVICE_START && command.operation <= USB_SERVICE_RECOVER)
        return UsbService_HardwareCommand(&command, result);

    //通过分发表执行合法命令；重复、迟到及非法请求只生成FAULT
    if (result == SYS_OK)
    {
        if (command.operation >= USB_SERVICE_OP_COUNT || s_handlers[command.operation] == NULL)
            result = SYS_ERR_NOT_SUPPORTED;
        else
            result = s_handlers[command.operation](context, &command);
    }
    return AppV3Mailbox_Complete(&context->mailbox, &command, result,
        context->simulated_running, context->packet_limit);
}

//准备本服务的配置、队列与事件订阅，再由Registry发布Record
SysResult_t AppV3_UsbInit(const AppV3Context_t *context)
{
    SysResult_t result;

    if (!App_MessagesIsOwner(RUNTIME_TARGET_USB))
        return SYS_ERR_INVALID_STATE;
    if (s_usb.config != NULL)
        return SYS_ERR_ALREADY_EXISTS;
    if (context == NULL || context->video == NULL)
        return SYS_ERR_INVALID_STATE;

    //准备阶段只检查输出选择，USB端点在本阶段不启动
    if (context->video->output != VIDEO_OUTPUT_USB_UVC)
        return SYS_ERR_NOT_SUPPORTED;
    result = AppV3Mailbox_Init(&s_usb.mailbox, RUNTIME_TARGET_USB, UsbService_Readable, &s_endpoint);
    if (result == SYS_OK)
    {
        s_usb.config = context->video;
        s_usb.hardware.state = APP_V3_HW_READY;
    }
    return result;
}

//Record发布失败时只回收未公开的队列和订阅
void AppV3_UsbDiscard(void)
{
    if (!App_MessagesIsOwner(RUNTIME_TARGET_USB))
        return;
    AppV3Mailbox_Discard(&s_usb.mailbox, &s_endpoint);
    s_usb.config = NULL;
    s_usb.hardware.state = APP_V3_HW_UNINITIALIZED;
}

//公开API持有Record期间提交值类型消息，发送方不接触私有Context
SysResult_t UsbService_Request(uint32_t command_id, UsbServiceOperation_t operation, uint32_t argument)
{
    return AppV3Mailbox_Request(RUNTIME_TARGET_USB, &s_endpoint, command_id, operation, argument);
}

//固定Registry指向本服务的实际线程入口
void AppV3_UsbRun(void *argument)
{
    AppV3_ServiceRun(argument, &s_usb.mailbox, AppV3_UsbAdvance);
}
