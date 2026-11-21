#include "usb_service.h"
#include "../../Tasks/service_tasks.h"

typedef struct
{
    AppV3ServiceMailbox_t mailbox;
    const VideoConfig_t *config;
    uint32_t packet_limit;
    bool simulated_running;
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

    result = AppV3Mailbox_Receive(&context->mailbox, &command);
    if (command.command_id == 0U || (uint32_t)command.requester >= RUNTIME_TARGET_COUNT)
        return result;

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
        s_usb.config = context->video;
    return result;
}

//Record发布失败时只回收未公开的队列和订阅
void AppV3_UsbDiscard(void)
{
    if (!App_MessagesIsOwner(RUNTIME_TARGET_USB))
        return;
    AppV3Mailbox_Discard(&s_usb.mailbox, &s_endpoint);
    s_usb.config = NULL;
}

//公开API持有Record期间提交值类型消息，发送方不接触私有Context
SysResult_t UsbService_Request(uint32_t command_id, UsbServiceOperation_t operation, uint32_t argument)
{
    return AppV3Mailbox_Request(RUNTIME_TARGET_USB, &s_endpoint, command_id, operation, argument);
}

//固定Registry指向本服务的实际线程入口
void AppV3_UsbRun(void *argument)
{
    AppV3_ServiceRun(argument, &s_usb.mailbox);
}
