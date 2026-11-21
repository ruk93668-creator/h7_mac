#include "storage_service.h"
#include "../../Tasks/service_tasks.h"

typedef struct
{
    AppV3ServiceMailbox_t mailbox;
    const AppStorageConfig_t *config;
    uint32_t sample_divisor;
    bool simulated_running;
} StorageServiceContext_t;

typedef SysResult_t (*StorageServiceHandler_t)(StorageServiceContext_t *context,
    const AppV3ServiceCommand_t *command);

//只有所属任务修改私有状态。发布后API只读取单独保存的队列入口
static StorageServiceContext_t s_storage;
static AppV3Queue_t s_endpoint;

//只改变模拟状态，保持实际设备状态口独立
static SysResult_t StorageService_Start(StorageServiceContext_t *context,
    const AppV3ServiceCommand_t *command)
{
    (void)command;
    if (context->simulated_running)
        return SYS_ERR_INVALID_STATE;
    context->simulated_running = true;
    return SYS_OK;
}

//模拟停止不会访问硬件
static SysResult_t StorageService_Stop(StorageServiceContext_t *context,
    const AppV3ServiceCommand_t *command)
{
    (void)command;
    if (!context->simulated_running)
        return SYS_ERR_INVALID_STATE;
    context->simulated_running = false;
    return SYS_OK;
}

//校验本服务的模拟参数，失败时保留原值
static SysResult_t StorageService_Parameter(StorageServiceContext_t *context,
    const AppV3ServiceCommand_t *command)
{
    if (command->argument == 0U || command->argument > 64U)
        return SYS_ERR_INVALID_ARG;
    context->sample_divisor = command->argument;
    return SYS_OK;
}

//完成消息携带当前模拟状态，不公开Context地址
static SysResult_t StorageService_Query(StorageServiceContext_t *context,
    const AppV3ServiceCommand_t *command)
{
    (void)context;
    (void)command;
    return SYS_OK;
}

//故障注入只生成软件FAULT
static SysResult_t StorageService_Fail(StorageServiceContext_t *context,
    const AppV3ServiceCommand_t *command)
{
    (void)context;
    (void)command;
    return SYS_ERR_IO;
}

static const StorageServiceHandler_t s_handlers[STORAGE_SERVICE_OP_COUNT] =
{
    [STORAGE_SERVICE_MOCK_START] = StorageService_Start,
    [STORAGE_SERVICE_MOCK_STOP] = StorageService_Stop,
    [STORAGE_SERVICE_SET_PARAMETER] = StorageService_Parameter,
    [STORAGE_SERVICE_QUERY] = StorageService_Query,
    [STORAGE_SERVICE_MOCK_FAIL] = StorageService_Fail
};

//一次可读回调只接收一条命令，Event Loop负责控制批次预算
static SysResult_t StorageService_Readable(AppV3Queue_t queue, void *argument)
{
    StorageServiceContext_t *context = &s_storage;
    AppV3ServiceCommand_t command = {0};
    SysResult_t result;
    (void)argument;

    //所属任务与队列都要匹配，禁止跨Service取消息
    if (!App_MessagesIsOwner(RUNTIME_TARGET_STORAGE) ||
        queue.index != context->mailbox.queue.index ||
        queue.generation != context->mailbox.queue.generation)
        return SYS_ERR_INVALID_STATE;

    result = AppV3Mailbox_Receive(&context->mailbox, &command);
    if (command.command_id == 0U || (uint32_t)command.requester >= RUNTIME_TARGET_COUNT)
        return result;

    //通过分发表执行合法命令；重复、迟到及非法请求只生成FAULT
    if (result == SYS_OK)
    {
        if (command.operation >= STORAGE_SERVICE_OP_COUNT || s_handlers[command.operation] == NULL)
            result = SYS_ERR_NOT_SUPPORTED;
        else
            result = s_handlers[command.operation](context, &command);
    }
    return AppV3Mailbox_Complete(&context->mailbox, &command, result,
        context->simulated_running, context->sample_divisor);
}

//准备本服务的配置、队列与事件订阅，再由Registry发布Record
SysResult_t AppV3_StorageInit(const AppV3Context_t *context)
{
    SysResult_t result;

    if (!App_MessagesIsOwner(RUNTIME_TARGET_STORAGE))
        return SYS_ERR_INVALID_STATE;
    if (s_storage.config != NULL)
        return SYS_ERR_ALREADY_EXISTS;
    if (context == NULL || context->storage == NULL)
        return SYS_ERR_INVALID_STATE;

    //仅检查软件采样配置，不访问SD卡
    if (context->storage->record_fps == 0U)
        return SYS_ERR_INVALID_ARG;
    result = AppV3Mailbox_Init(&s_storage.mailbox, RUNTIME_TARGET_STORAGE, StorageService_Readable, &s_endpoint);
    if (result == SYS_OK)
        s_storage.config = context->storage;
    return result;
}

//Record发布失败时只回收未公开的队列和订阅
void AppV3_StorageDiscard(void)
{
    if (!App_MessagesIsOwner(RUNTIME_TARGET_STORAGE))
        return;
    AppV3Mailbox_Discard(&s_storage.mailbox, &s_endpoint);
    s_storage.config = NULL;
}

//公开API持有Record期间提交值类型消息，发送方不接触私有Context
SysResult_t StorageService_Request(uint32_t command_id, StorageServiceOperation_t operation, uint32_t argument)
{
    return AppV3Mailbox_Request(RUNTIME_TARGET_STORAGE, &s_endpoint, command_id, operation, argument);
}

//固定Registry指向本服务的实际线程入口
void AppV3_StorageRun(void *argument)
{
    AppV3_ServiceRun(argument, &s_storage.mailbox);
}
