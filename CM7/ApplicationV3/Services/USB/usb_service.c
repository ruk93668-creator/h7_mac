#include "usb_service.h"
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
    if (!s_usb.class_registered)
    {
        result = USB_UVC_Register();
        if (result != SYS_OK)
            return result;
        s_usb.class_registered = true;
    }
    //Start失败仍可能留下待STOP的设备，不能用Configured=false确认资源释放
    result = USB_Device_Start();
    if (result != SYS_ERR_BUSY)
    {
        s_usb.stop_required = true;
        s_usb.stop_applied = false;
    }
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

static SysResult_t UsbService_ObserveDevice(void)
{
    SysResult_t result;
    if (USB_Device_IsConfigured())
        s_usb.hardware.flags |= USB_HW_CONFIGURED;
    else
        s_usb.hardware.flags &= (uint16_t)~USB_HW_CONFIGURED;
    USB_UVC_Process();
    result = USB_Device_GetLastResult();
    if (result == SYS_OK && (s_usb.hardware.flags & USB_HW_CONFIGURED) != 0U)
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
        result = UsbService_ObserveDevice();
        if ((s_usb.hardware.flags & USB_HW_CONFIGURED) == 0U ||
            (result != SYS_OK && result != SYS_ERR_BUSY))
        {
            s_usb.hardware.first_fault = result == SYS_OK || result == SYS_ERR_BUSY ? SYS_ERR_IO : result;
            s_usb.hardware.state = APP_V3_HW_FAULT;
        }
        return USB_HW_POLL_MS;
    }
    if (!s_usb.request.pending)
        return APP_EVENTS_WAIT_FOREVER;

    now = Platform_TimeGetMs();
    if (s_usb.hardware.state == APP_V3_HW_STARTING)
    {
        result = now - s_usb.request.started_ms >= USB_HW_ENUM_TIMEOUT_MS ? SYS_ERR_TIMEOUT :
            (s_usb.waiting_configured ? UsbService_ObserveDevice() : UsbService_BeginDevice());
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
