#include "storage_service.h"
#include "../../Tasks/service_tasks.h"
#include "storage_fs.h"
#include "sd_block.h"
#include "platform_time.h"
#include "mjpeg_avi.h"


//仅由Storage任务推进的卷和录像生命周期
typedef struct
{
    //所属服务独占的命令邮箱及回执暂存
    AppV3ServiceMailbox_t mailbox;
    //初始化阶段提供的只读配置，生命周期内由所属模块读取
    const AppStorageConfig_t *config;
    //由服务发布的硬件状态快照
    AppV3HardwareReport_t hardware;
    //正在推进的硬件命令及起始时间
    AppV3HardwareRequest_t request;
    //已完成本服务的卷挂载，需要通过原路径卸载
    bool mount_committed;
    //录像使用的采集配置或监督器观察到的Camera结果
    const CameraConfig_t *camera;
} StorageServiceContext_t;

//只有所属任务修改私有状态。发布后API只读取单独保存的队列入口
static StorageServiceContext_t s_storage;
//只保存对外可提交的队列入口，私有上下文仍由服务任务写入
static AppV3Queue_t s_endpoint;

//录像帧从申请到归还确认的推进状态
typedef enum
{
    STORAGE_FRAME_IDLE = 0,
    STORAGE_FRAME_WAIT_OFFER,
    STORAGE_FRAME_CANCEL,
    STORAGE_FRAME_CANCEL_SENT,
    STORAGE_FRAME_RETURN,
    STORAGE_FRAME_WAIT_ACK,
    STORAGE_FRAME_CONFIRM
} StorageFrameState_t;

//Task私有，持有传输身份至最后一条确认实际入队。
static struct
{
    //由Storage独占推进的AVI文件索引及写入状态
    MjpegAvi_t avi;
    //本次操作持有的文件句柄，关闭后不得继续使用
    StorageFile_t file;
    //Storage入队、归还及确认的当前阶段
    StorageFrameState_t frame_state;
    //已经受理的帧消息，保留归还交接所需身份
    AppMessage_t transfer;
    //当前在途帧请求的编号
    uint32_t frame_request;
    //创建新录像文件使用的唯一编号，由发起者提供
    uint32_t file_number;
    //按录像节奏允许下一次取帧的时间点
    uint32_t sample_at;
    //本事务首个错误，后续清理结果不得覆盖
    SysResult_t first_fault;
    //是否允许发起新的录像帧申请
    bool admission;
    //已停止准入并等待在途帧、文件和卷退出
    bool draining;
} s_record;


//Storage硬件操作轮询间隔，单位毫秒
#define STORAGE_HW_POLL_MS 50U
//录像活动期间的进度轮询间隔，单位毫秒
#define STORAGE_RECORD_POLL_MS 10U
//Storage硬件命令的正常等待上限，单位毫秒
#define STORAGE_HW_TIMEOUT_MS 5000U
//硬件当前报告卡在位
#define STORAGE_HW_CARD_PRESENT 1U
//文件系统仍持有块设备独占凭据
#define STORAGE_HW_FATFS_OWNER 2U
//确认卷已卸载且I/O所有权已退出
#define STORAGE_HW_IO_QUIET 4U
//StorageFsDiagnostics_t公开状态值
#define STORAGE_DIAG_READY 1U
//诊断报告中卷已挂载的状态值
#define STORAGE_DIAG_MOUNTED 2U
//诊断报告中卷需要恢复的状态值
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

//Mount确认调用指定的权限，普通START仍只读。
static SysResult_t StorageService_Mount(bool writable)
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
        result = StorageFS_Mount(writable, s_storage.config->partition);
        if (result != SYS_OK)
            return result;
        s_storage.mount_committed = true;
    }
    //挂载完成必须有独占租约及实际只读卷事实，不能只看返回值
    result = StorageFS_GetDiagnostics(&facts);
    if (result != SYS_OK)
        return result;
    if (facts.state != STORAGE_DIAG_MOUNTED || (facts.writable != 0U) != writable ||
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

//冻结录像首错；仍通过原FIFO推进在途归还。
static void StorageRecord_Fault(SysResult_t result)
{
    if (s_record.first_fault == SYS_OK)
        s_record.first_fault = result;
    if (s_storage.hardware.first_fault == SYS_OK)
        s_storage.hardware.first_fault = result;
    s_record.admission = false;
    s_record.draining = true;
}

//使用固定8位十六进制文件号；CREATE_NEW保证不会覆盖已有录像。
static void StorageRecord_Path(char path[16], uint32_t number)
{
    static const char digits[] = "0123456789ABCDEF";
    uint32_t i;

    path[0] = '0';
    path[1] = ':';
    path[2] = '/';
    for (i = 0U; i < 8U; ++i)
        path[3U + i] = digits[(number >> (28U - 4U * i)) & 15U];
    path[11] = '.';
    path[12] = 'A';
    path[13] = 'V';
    path[14] = 'I';
    path[15] = '\0';
}

//直到第一次实际帧才创建文件，开始后尚无帧就停止不会留下无索引AVI。
static SysResult_t StorageRecord_Append(const AppMessage_t *message)
{
    const CameraFrame_t *frame = &message->frame;
    SysResult_t result;
    char path[16];

    if (frame->format != CAMERA_PIXEL_FORMAT_JPEG || frame->width != s_storage.camera->width ||
        frame->height != s_storage.camera->height || frame->size_bytes2 >= frame->size_bytes)
        return SYS_ERR_PROTOCOL;
    if (s_record.file == 0U)
    {
        StorageRecord_Path(path, s_record.file_number);
        result = StorageFS_Open(path, STORAGE_FS_OPEN_WRITE | STORAGE_FS_OPEN_CREATE_NEW, &s_record.file);
        if (result != SYS_OK)
            return result;
        result = MjpegAvi_Begin(&s_record.avi, s_record.file, frame->width, frame->height,
            (uint16_t)s_storage.config->record_fps);
        if (result != SYS_OK)
            return result;
    }
    if (s_storage.config->segment_ms != 0U && s_record.avi.frames != 0U &&
        message->timestamp_ms - s_record.avi.first_tick >= s_storage.config->segment_ms)
    {
        s_record.admission = false;
        s_record.draining = true;
        return SYS_OK;
    }

    //原StorageFS同步复制每段到自有输入区；函数返回后，无DMA能读取原JPEG span。
    result = MjpegAvi_AppendFrame(&s_record.avi, frame, message->timestamp_ms, s_storage.config->segment_bytes);
    if (result == SYS_ERR_NO_RESOURCE && !s_record.avi.failed && s_record.avi.frames != 0U)
    {
        s_record.admission = false;
        s_record.draining = true;
        return SYS_OK;
    }
    return result;
}

//请求号、会话和Loan编号限定回复的生命周期。同步写结束后才产生RETURN。
static void StorageRecord_Receive(const AppMessage_t *message)
{
    SysResult_t result;

    if (message->source != RUNTIME_TARGET_CAMERA || message->command_id != s_record.frame_request)
        return;
    if (message->type == APP_MESSAGE_RETURN_ACK && s_record.frame_state == STORAGE_FRAME_WAIT_ACK &&
        message->result == SYS_OK && message->stream_epoch == s_record.transfer.stream_epoch &&
        message->lease_id == s_record.transfer.lease_id)
    {
        s_record.frame_state = STORAGE_FRAME_CONFIRM;
        return;
    }
    if (message->type != APP_MESSAGE_FRAME_OFFER || (s_record.frame_state != STORAGE_FRAME_WAIT_OFFER &&
        s_record.frame_state != STORAGE_FRAME_CANCEL && s_record.frame_state != STORAGE_FRAME_CANCEL_SENT))
        return;

    if (message->result != SYS_OK && message->stream_epoch == 0U && message->lease_id == 0U)
    {
        if (s_record.admission)
            StorageRecord_Fault(message->result);
        s_record.frame_state = STORAGE_FRAME_CONFIRM;
    }
    else if (message->result == SYS_OK && message->stream_epoch != 0U && message->lease_id != 0U)
    {
        if (s_record.admission && s_record.frame_state == STORAGE_FRAME_WAIT_OFFER)
        {
            result = StorageRecord_Append(message);
            if (result != SYS_OK)
                StorageRecord_Fault(result);
        }
        s_record.frame_state = STORAGE_FRAME_RETURN;
    }
    else
        return;

    //只保存交接身份，归还阶段不再保存帧地址。
    s_record.transfer = (AppMessage_t){.stream_epoch = message->stream_epoch, .lease_id = message->lease_id};
}

//每轮最多提交一条消息。即使没有通知也定期重试原身份。
static void StorageRecord_Send(uint32_t now)
{
    AppMessage_t message = {.source = RUNTIME_TARGET_STORAGE, .command_id = s_record.frame_request};
    StorageFrameState_t next;

    switch (s_record.frame_state)
    {
        case STORAGE_FRAME_IDLE:
            if (!s_record.admission || now - s_record.sample_at < 1000U / s_storage.config->record_fps)
                return;
            if (s_record.frame_request == UINT32_MAX)
            {
                StorageRecord_Fault(SYS_ERR_NO_RESOURCE);
                return;
            }
            message.type = APP_MESSAGE_FRAME_REQUEST;
            ++message.command_id;
            next = STORAGE_FRAME_WAIT_OFFER;
            break;
        case STORAGE_FRAME_CANCEL:
            message.type = APP_MESSAGE_FRAME_CANCEL_REQUEST;
            next = STORAGE_FRAME_CANCEL_SENT;
            break;
        case STORAGE_FRAME_RETURN:
            message.type = APP_MESSAGE_FRAME_RETURN;
            next = STORAGE_FRAME_WAIT_ACK;
            break;
        case STORAGE_FRAME_CONFIRM:
            message.type = APP_MESSAGE_RETURN_ACK_CONSUMED;
            next = STORAGE_FRAME_IDLE;
            break;
        default:
            return;
    }
    message.stream_epoch = s_record.transfer.stream_epoch;
    message.lease_id = s_record.transfer.lease_id;
    if (App_MessagesSend(APP_CHANNEL_STORAGE_TO_CAMERA, &message) != SYS_OK)
        return;
    s_record.frame_state = next;
    if (message.type == APP_MESSAGE_FRAME_REQUEST)
    {
        s_record.frame_request = message.command_id;
        s_record.sample_at = now;
    }
    if (next == STORAGE_FRAME_IDLE)
        s_record.transfer = (AppMessage_t){.command_id = 0U};
}

//处理录像帧写入与归还，并按准入状态申请下一帧
static void StorageRecord_Advance(uint32_t now)
{
    AppMessage_t message;
    uint32_t count;

    if (!s_record.admission && s_record.frame_state == STORAGE_FRAME_WAIT_OFFER)
        s_record.frame_state = STORAGE_FRAME_CANCEL;
    for (count = 0U; count < 4U && App_MessagesReceive(APP_CHANNEL_CAMERA_TO_STORAGE, &message); ++count)
        StorageRecord_Receive(&message);
    StorageRecord_Send(now);
}

//先排空交接，再收尾AVI和关闭文件，最后由既有卷停止核验确认SD状态。
static SysResult_t StorageRecord_Close(bool recover)
{
    StorageFsDiagnostics_t facts;
    SysResult_t result;

    if (s_record.frame_state != STORAGE_FRAME_IDLE)
        return SYS_ERR_BUSY;
    if (s_record.file != 0U)
    {
        result = StorageFS_GetDiagnostics(&facts);
        if (result != SYS_OK)
            return result;
        if (recover && facts.state == STORAGE_DIAG_FAULT)
        {
            //只使用原卷恢复：它负责撤销旧文件凭据并确认独占租约已退出。
            result = StorageFS_Recover();
            if (result != SYS_OK)
                return result;
            s_record.file = 0U;
        }
        else
        {
            if (s_record.first_fault == SYS_OK && !s_record.avi.finished)
            {
                result = MjpegAvi_Finish(&s_record.avi);
                if (result != SYS_OK)
                    StorageRecord_Fault(result);
            }
            result = StorageFS_Close(s_record.file);
            if (result != SYS_OK)
                return result;
            s_record.file = 0U;
        }
    }
    result = StorageService_CloseVolume(recover);
    if (result == SYS_OK)
        s_record.draining = false;
    return result;
}

//返回成功写入数量以及当前录像资源状态
static SysResult_t StorageRecord_Query(const AppV3ServiceCommand_t *command)
{
    AppV3HardwareReport_t report;
    SysResult_t result = StorageService_Observe(s_storage.hardware.state == APP_V3_HW_ACTIVE);

    report = s_storage.hardware;
    report.primary = s_record.avi.frames;
    if (s_record.admission)
        report.flags |= STORAGE_RECORD_ACTIVE;
    if (s_record.file != 0U)
        report.flags |= STORAGE_RECORD_FILE;
    if (s_record.draining)
        report.flags |= STORAGE_RECORD_DRAIN;
    return AppV3Mailbox_CompleteHardware(&s_storage.mailbox, command, result, &report);
}

//真实操作由后续Storage轮次执行，保持原请求身份和背压
static SysResult_t StorageService_HardwareCommand(const AppV3ServiceCommand_t *command, SysResult_t result)
{
    AppV3HardwareState_t state = s_storage.hardware.state;

    if (result != SYS_OK)
        return AppV3Mailbox_CompleteHardware(&s_storage.mailbox, command, result, &s_storage.hardware);
    if (command->operation == STORAGE_SERVICE_RECORD_QUERY)
        return StorageRecord_Query(command);
    if (command->operation == STORAGE_SERVICE_START || command->operation == STORAGE_SERVICE_RECORD_START)
    {
        //原START只读。只有独立录像START才允许请求写权限。
        if (command->operation == STORAGE_SERVICE_RECORD_START)
        {
            if (s_storage.camera == NULL || s_storage.camera->format != CAMERA_PIXEL_FORMAT_JPEG)
                result = SYS_ERR_NOT_SUPPORTED;
            else if (command->argument == 0U)
                result = SYS_ERR_INVALID_ARG;
            if (result != SYS_OK)
                return AppV3Mailbox_CompleteHardware(&s_storage.mailbox, command, result, &s_storage.hardware);
        }

        if (state != APP_V3_HW_READY)
            result = state == APP_V3_HW_ACTIVE ? SYS_ERR_ALREADY_EXISTS : SYS_ERR_INVALID_STATE;
        else
            s_storage.hardware = (AppV3HardwareReport_t){.state = APP_V3_HW_STARTING};
    }
    else if (command->operation != STORAGE_SERVICE_HARDWARE_QUERY)
    {
        if (state == APP_V3_HW_ACTIVE || state == APP_V3_HW_FAULT || state == APP_V3_HW_QUIESCING)
        {
            s_record.admission = false;
            s_storage.hardware.state = APP_V3_HW_QUIESCING;
        }
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
static uint32_t StorageService_Advance(void)
{
    SysResult_t result;
    uint32_t operation;
    AppV3HardwareState_t state;

    if (!App_MessagesIsOwner(RUNTIME_TARGET_STORAGE))
        return APP_EVENTS_WAIT_FOREVER;
    state = s_storage.hardware.state;
    if (state == APP_V3_HW_ACTIVE && s_record.admission)
    {
        result = StorageService_Observe(false);
        if (result != SYS_OK && result != SYS_ERR_BUSY)
            StorageRecord_Fault(result);
    }
    StorageRecord_Advance(Platform_TimeGetMs());

    if (!s_storage.request.pending)
    {
        if (s_record.draining && state != APP_V3_HW_FAULT)
        {
            s_storage.hardware.state = APP_V3_HW_QUIESCING;
            result = StorageRecord_Close(false);
            if (result != SYS_ERR_BUSY)
            {
                if (result != SYS_OK)
                    StorageRecord_Fault(result);
                s_storage.hardware.state = s_record.first_fault == SYS_OK ?
                    APP_V3_HW_READY : APP_V3_HW_FAULT;
            }
            return STORAGE_HW_POLL_MS;
        }
        if (s_record.admission || s_record.frame_state != STORAGE_FRAME_IDLE)
            return STORAGE_RECORD_POLL_MS;
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
    else if (operation == STORAGE_SERVICE_START || operation == STORAGE_SERVICE_RECORD_START)
    {
        bool writable = operation == STORAGE_SERVICE_RECORD_START;
        result = StorageService_Mount(writable);
        if (result == SYS_OK)
            s_record.first_fault = SYS_OK;
        if (result == SYS_OK && writable)
        {
            s_record.avi = (MjpegAvi_t){0};
            s_record.file_number = s_storage.request.command.argument;
            s_record.sample_at = Platform_TimeGetMs() - 1000U / s_storage.config->record_fps;
            s_record.admission = true;
        }
    }
    else
    {
        result = StorageRecord_Close(operation == STORAGE_SERVICE_RECOVER);
        if (result == SYS_OK && operation != STORAGE_SERVICE_RECOVER && s_record.first_fault != SYS_OK)
            result = s_record.first_fault;
    }
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
        s_storage.hardware.state = operation == STORAGE_SERVICE_START || operation == STORAGE_SERVICE_RECORD_START ?
            APP_V3_HW_ACTIVE : APP_V3_HW_READY;
    s_storage.request.pending = false;
    (void)AppV3Mailbox_CompleteHardware(&s_storage.mailbox, &s_storage.request.command, result, &s_storage.hardware);
    return STORAGE_HW_POLL_MS;
}

//由Storage任务推进异步硬件命令及录像退出
uint32_t AppV3_StorageAdvance(void)
{
    uint32_t wait = StorageService_Advance();

    return wait;
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

    if (command.operation >= STORAGE_SERVICE_START && command.operation <= STORAGE_SERVICE_RECORD_QUERY)
        return StorageService_HardwareCommand(&command, result);

    if (result == SYS_OK)
        result = SYS_ERR_NOT_SUPPORTED;
    return AppV3Mailbox_Complete(&context->mailbox, &command, result, false, 0U);
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
    if (context->storage->record_fps == 0U || context->storage->record_fps > 60U)
        return SYS_ERR_INVALID_ARG;
    result = AppV3Mailbox_Init(&s_storage.mailbox, RUNTIME_TARGET_STORAGE, StorageService_Readable, &s_endpoint);
    if (result == SYS_OK)
    {
        s_storage.config = context->storage;
        s_storage.camera = context->video == NULL ? NULL : &context->video->camera;
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
    s_storage.camera = NULL;
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
