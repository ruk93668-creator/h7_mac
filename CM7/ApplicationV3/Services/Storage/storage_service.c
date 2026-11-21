#include "storage_service.h"
#include "../../Tasks/service_tasks.h"
#include "storage_fs.h"
#include "sd_block.h"
#include "platform_time.h"


typedef struct
{
    AppV3ServiceMailbox_t mailbox;
    const AppStorageConfig_t *config;
    uint32_t sample_divisor;
    bool simulated_running;
    AppV3HardwareReport_t hardware;
    AppV3HardwareRequest_t request;
    bool mount_committed;
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

#define STORAGE_HW_POLL_MS 50U
#define STORAGE_HW_TIMEOUT_MS 5000U
#define STORAGE_HW_CARD_PRESENT 1U
#define STORAGE_HW_FATFS_OWNER 2U
#define STORAGE_HW_IO_QUIET 4U
//StorageFsDiagnostics_t公开状态值
#define STORAGE_DIAG_READY 1U
#define STORAGE_DIAG_MOUNTED 2U
#define STORAGE_DIAG_FAULT 3U

//只读诊断不夺取RAW或FATFS租约
static SysResult_t StorageService_Observe(bool query_capacity)
{
    SdBlockStatus_t block;
    uint32_t detect;
    bool present;
    SysResult_t result = BSP_SDMMC_ReadDetect(&detect, &present);

    if (result != SYS_OK)
        return result;
    s_storage.hardware.flags = present ? STORAGE_HW_CARD_PRESENT : 0U;
    if (!SD_Block_IsOwner(SD_BLOCK_CLIENT_FATFS))
        return s_storage.hardware.state == APP_V3_HW_ACTIVE ? SYS_ERR_INVALID_STATE : SYS_OK;

    s_storage.hardware.flags |= STORAGE_HW_FATFS_OWNER;
    result = SD_Block_GetStatus(&block);
    if (result != SYS_OK)
        return result;
    if (block.busy)
        return SYS_ERR_BUSY;
    if (!block.present || !block.initialized || block.fault)
        return !block.present ? SYS_ERR_NOT_FOUND : SYS_ERR_IO;
    s_storage.hardware.flags |= STORAGE_HW_IO_QUIET;

    if (query_capacity)
    {
        uint64_t total_bytes, free_bytes;
        result = StorageFS_GetSpace(&total_bytes, &free_bytes);
        if (result != SYS_OK)
            return result;
        if (total_bytes / 512U > UINT32_MAX || free_bytes / 512U > UINT32_MAX)
            return SYS_ERR_NO_RESOURCE;
        s_storage.hardware.primary = (uint32_t)(total_bytes / 512U);
        s_storage.hardware.detail = (uint32_t)(free_bytes / 512U);
    }
    return SYS_OK;
}

//Mount内部完成介质初始化与租约申请。本阶段始终只读
static SysResult_t StorageService_MountReadonly(void)
{
    StorageFsDiagnostics_t facts;
    SysResult_t result;
    uint32_t detect;
    bool present;

    if (!s_storage.mount_committed)
    {
        result = BSP_SDMMC_ReadDetect(&detect, &present);
        if (result != SYS_OK || !present)
            return result != SYS_OK ? result : SYS_ERR_NOT_FOUND;
        result = StorageFS_Mount(false, s_storage.config->partition);
        if (result != SYS_OK)
            return result;
        s_storage.mount_committed = true;
    }
    //挂载完成必须有独占租约及实际只读卷事实，不能只看返回值
    result = StorageFS_GetDiagnostics(&facts);
    if (result != SYS_OK)
        return result;
    if (facts.state != STORAGE_DIAG_MOUNTED || facts.writable != 0U ||
        !SD_Block_IsOwner(SD_BLOCK_CLIENT_FATFS))
        return SYS_ERR_PROTOCOL;
    return StorageService_Observe(false);
}

//确认无打开文件和已释放租约；失败时保留本Service的故障
static SysResult_t StorageService_CloseVolume(bool recover)
{
    StorageFsDiagnostics_t facts;
    SysResult_t result = StorageFS_GetDiagnostics(&facts);

    if (result != SYS_OK)
        return result;
    if (facts.open_files != 0U)
        return SYS_ERR_BUSY;
    if (facts.state == STORAGE_DIAG_MOUNTED)
    {
        result = StorageFS_Unmount();
        //拔卡可能先令原Unmount进入FS_FAULT，RECOVER下一轮继续走原恢复入口
        if (result != SYS_OK && result != SYS_ERR_BUSY && recover &&
            StorageFS_GetDiagnostics(&facts) == SYS_OK && facts.state == STORAGE_DIAG_FAULT)
        {
            if (s_storage.hardware.first_fault == SYS_OK)
                s_storage.hardware.first_fault = result;
            return SYS_ERR_BUSY;
        }
    }
    else if (facts.state == STORAGE_DIAG_FAULT && recover)
        result = StorageFS_Recover();
    else if (facts.state != STORAGE_DIAG_READY)
        return SYS_ERR_INVALID_STATE;

    if (result != SYS_OK)
        return result;
    result = StorageFS_GetDiagnostics(&facts);
    if (result != SYS_OK)
        return result;
    if (facts.state != STORAGE_DIAG_READY || facts.open_files != 0U ||
        SD_Block_IsOwner(SD_BLOCK_CLIENT_FATFS) || facts.cleanup_result != SYS_OK)
        return SYS_ERR_BUSY;
    s_storage.mount_committed = false;
    s_storage.hardware.primary = 0U;
    s_storage.hardware.detail = 0U;
    s_storage.hardware.flags = STORAGE_HW_IO_QUIET;
    return SYS_OK;
}

//真实操作由后续Storage轮次执行，保持原请求身份和背压
static SysResult_t StorageService_HardwareCommand(const AppV3ServiceCommand_t *command, SysResult_t result)
{
    AppV3HardwareState_t state = s_storage.hardware.state;

    if (result != SYS_OK)
        return AppV3Mailbox_CompleteHardware(&s_storage.mailbox, command, result, &s_storage.hardware);
    if (command->operation == STORAGE_SERVICE_START)
    {
        if (state != APP_V3_HW_READY)
            result = state == APP_V3_HW_ACTIVE ? SYS_ERR_ALREADY_EXISTS : SYS_ERR_INVALID_STATE;
        else
            s_storage.hardware = (AppV3HardwareReport_t){.state = APP_V3_HW_STARTING};
    }
    else if (command->operation != STORAGE_SERVICE_HARDWARE_QUERY)
    {
        if (state == APP_V3_HW_ACTIVE || state == APP_V3_HW_FAULT)
            s_storage.hardware.state = APP_V3_HW_QUIESCING;
        else
            return AppV3Mailbox_CompleteHardware(&s_storage.mailbox, command,
                state == APP_V3_HW_READY ? SYS_OK : SYS_ERR_INVALID_STATE, &s_storage.hardware);
    }
    if (result != SYS_OK)
        return AppV3Mailbox_CompleteHardware(&s_storage.mailbox, command, result, &s_storage.hardware);
    s_storage.request = (AppV3HardwareRequest_t){*command, Platform_TimeGetMs(), true};
    return SYS_OK;
}

//使用原驱动的有界等待，Service只在后续轮次重试BUSY
uint32_t AppV3_StorageAdvance(void)
{
    SysResult_t result;
    uint32_t operation;
    AppV3HardwareState_t state;

    if (!App_MessagesIsOwner(RUNTIME_TARGET_STORAGE))
        return APP_EVENTS_WAIT_FOREVER;
    state = s_storage.hardware.state;
    if (!s_storage.request.pending)
    {
        if (state != APP_V3_HW_ACTIVE)
            return APP_EVENTS_WAIT_FOREVER;
        result = StorageService_Observe(false);
        if (result != SYS_OK && result != SYS_ERR_BUSY)
        {
            s_storage.hardware.first_fault = result;
            s_storage.hardware.state = APP_V3_HW_FAULT;
        }
        return STORAGE_HW_POLL_MS;
    }

    operation = s_storage.request.command.operation;
    if (operation == STORAGE_SERVICE_HARDWARE_QUERY)
        result = StorageService_Observe(state == APP_V3_HW_ACTIVE);
    else if (operation == STORAGE_SERVICE_START)
        result = StorageService_MountReadonly();
    else
        result = StorageService_CloseVolume(operation == STORAGE_SERVICE_RECOVER);
    if (result == SYS_ERR_BUSY &&
        Platform_TimeGetMs() - s_storage.request.started_ms < STORAGE_HW_TIMEOUT_MS)
        return STORAGE_HW_POLL_MS;
    if (result == SYS_ERR_BUSY)
        result = SYS_ERR_TIMEOUT;

    //查询失败不伪造已卸载；实际I/O故障进入FAULT并保留租约
    if (result != SYS_OK)
    {
        if (s_storage.hardware.first_fault == SYS_OK)
            s_storage.hardware.first_fault = result;
        if (operation != STORAGE_SERVICE_HARDWARE_QUERY || state == APP_V3_HW_ACTIVE)
            s_storage.hardware.state = APP_V3_HW_FAULT;
    }
    else if (operation != STORAGE_SERVICE_HARDWARE_QUERY)
        s_storage.hardware.state = operation == STORAGE_SERVICE_START ? APP_V3_HW_ACTIVE : APP_V3_HW_READY;
    s_storage.request.pending = false;
    (void)AppV3Mailbox_CompleteHardware(&s_storage.mailbox, &s_storage.request.command, result, &s_storage.hardware);
    return STORAGE_HW_POLL_MS;
}

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

    if (context->request.pending)
        return SYS_ERR_BUSY;
    result = AppV3Mailbox_Receive(&context->mailbox, &command);
    if (command.command_id == 0U || (uint32_t)command.requester >= RUNTIME_TARGET_COUNT)
        return result;

    if (command.operation >= STORAGE_SERVICE_START && command.operation <= STORAGE_SERVICE_RECOVER)
        return StorageService_HardwareCommand(&command, result);

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
    {
        s_storage.config = context->storage;
        s_storage.hardware.state = APP_V3_HW_READY;
    }
    return result;
}

//Record发布失败时只回收未公开的队列和订阅
void AppV3_StorageDiscard(void)
{
    if (!App_MessagesIsOwner(RUNTIME_TARGET_STORAGE))
        return;
    AppV3Mailbox_Discard(&s_storage.mailbox, &s_endpoint);
    s_storage.config = NULL;
    s_storage.hardware.state = APP_V3_HW_UNINITIALIZED;
}

//公开API持有Record期间提交值类型消息，发送方不接触私有Context
SysResult_t StorageService_Request(uint32_t command_id, StorageServiceOperation_t operation, uint32_t argument)
{
    return AppV3Mailbox_Request(RUNTIME_TARGET_STORAGE, &s_endpoint, command_id, operation, argument);
}

//固定Registry指向本服务的实际线程入口
void AppV3_StorageRun(void *argument)
{
    AppV3_ServiceRun(argument, &s_storage.mailbox, AppV3_StorageAdvance);
}
