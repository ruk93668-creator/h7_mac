#include "camera_service.h"
#include "../../Tasks/service_tasks.h"
#include "camera_capture.h"
#include "ov5640_device.h"
#include "platform_time.h"
#include "../../Core/app_messages.h"


//仅由CameraTask写入的采集服务上下文
typedef struct
{
    //所属服务独占的命令邮箱及回执暂存
    AppV3ServiceMailbox_t mailbox;
    //初始化阶段提供的只读配置，生命周期内由所属模块读取
    const CameraConfig_t *config;
    //由服务发布的硬件状态快照
    AppV3HardwareReport_t hardware;
    //正在推进的硬件命令及起始时间
    AppV3HardwareRequest_t request;
    //由CameraTask控制的传感器实例
    Ov5640Device_t *sensor;
    //请求前观察的停止序号，用于排除旧停止结果
    uint32_t stop_baseline;
    //记录启动失败的原因，供停止回退后报告
    SysResult_t rollback_fault;
    //传感器已完成格式配置的标志
    bool sensor_configured;
    //传感器已启用并可能需要关闭的标志
    bool sensor_started;
    //Media采集上下文有效的标志
    bool media_initialized;
    //已尝试启动采集，失败时也可能需清理硬件
    bool capture_attempted;
    //正在等待硬件给出首帧有效的事实
    bool waiting_frame;
    //当前任务还持有未归还的自检或读取帧
    bool frame_held;
} CameraServiceContext_t;

//只有所属任务修改私有状态。发布后API只读取单独保存的队列入口
static CameraServiceContext_t s_camera;
//只保存对外可提交的队列入口，私有上下文仍由服务任务写入
static AppV3Queue_t s_endpoint;

//Camera硬件进度轮询间隔，单位毫秒
#define CAMERA_HW_POLL_MS 10U
//Camera硬件操作等待上限，单位毫秒
#define CAMERA_HW_TIMEOUT_MS 3000U
//已经观察到实际采集完成的首帧
#define CAMERA_HW_FRAME_SEEN 1U
//自检帧仍由Camera持有
#define CAMERA_HW_FRAME_HELD 2U
//Media已证明采集硬件停止
#define CAMERA_HW_MEDIA_STOPPED 4U
//底层设备或采集发生错误
#define CAMERA_HW_DEVICE_ERROR 8U

//每个消费者一个有界交接记录。Media才拥有物理槽，Dispatcher只保存凭据。
typedef enum
{
    CAMERA_TRANSFER_EMPTY = 0,
    CAMERA_TRANSFER_REQUESTED,
    CAMERA_TRANSFER_OFFER_PENDING,
    CAMERA_TRANSFER_PUBLISHED,
    CAMERA_TRANSFER_RELEASING,
    CAMERA_TRANSFER_TERMINAL_PENDING,
    CAMERA_TRANSFER_WAIT_CONSUMED
} CameraTransferState_t;

//单个消费者的帧交接记录，保持凭据直到事务闭合
typedef struct
{
    //区分待投递和已公开租约，停止时只有未公开帧可撤回
    CameraTransferState_t state;
    union
    {
        CameraFrameLease_t raw;
        CameraMjpegLease_t jpeg;
    } lease;
    //选用JPEG物理租约的标志，决定归还接口
    bool jpeg;
    //待投递或等待消费确认的业务消息
    AppMessage_t reply;
    //该消费者最近受理的帧申请号
    uint32_t last_request;
    //已开放或已受理的帧准入事实
    bool admitted;
    //未公开帧在停止时归还后发送拒绝终结
    bool reject_offer;
} CameraTransfer_t;

//按消费者身份向固定下行通道投递消息
typedef SysResult_t (*CameraFrameSend_t)(CameraFrameConsumer_t consumer, const AppMessage_t *message);

//各消费者当前的交接状态及租约凭据
static CameraTransfer_t s_transfers[CAMERA_FRAME_CONSUMER_COUNT];
//分发帧的递增序号，耗尽时拒绝继续发放
static uint32_t s_frame_sequence;
//下次扫描的起点，用于轮换申请者优先顺序
static uint8_t s_next_consumer;

//CameraTask将每路回复发往所属消费者，不共享下行队首。
static SysResult_t CameraFrame_Send(CameraFrameConsumer_t consumer, const AppMessage_t *message)
{
    if (consumer == CAMERA_FRAME_CONSUMER_STORAGE)
        return App_MessagesSend(APP_CHANNEL_CAMERA_TO_STORAGE, message);
    if (consumer != CAMERA_FRAME_CONSUMER_USB)
        return SYS_ERR_NOT_SUPPORTED;
    return App_MessagesSend(APP_CHANNEL_TO_USB, message);
}

//只有完整的事务标识能改变已发布租约；帧序号和图像地址均不参与归还
static bool CameraFrame_Matches(const CameraTransfer_t *transfer, const AppMessage_t *message)
{
    return message->command_id == transfer->last_request &&
        message->stream_epoch == transfer->reply.stream_epoch &&
        message->lease_id == transfer->reply.lease_id;
}

//保留当前请求身份并准备确认或拒绝的终结消息
static void CameraFrame_Terminal(CameraTransfer_t *transfer, AppMessageType_t type, SysResult_t result)
{
    //清除View。成功归还后任何回复都不得再携带旧物理地址。
    transfer->reply.type = type;
    transfer->reply.result = result;
    transfer->reply.frame = (CameraFrame_t){0};
    transfer->state = CAMERA_TRANSFER_TERMINAL_PENDING;
}

//核对来源及请求凭据后处理消费者申请和归还
static void CameraFrame_Receive(CameraFrameConsumer_t consumer, const AppMessage_t *message, bool admission)
{
    CameraTransfer_t *transfer = &s_transfers[consumer];

    if (message->result != SYS_OK)
        return;
    if (message->type == APP_MESSAGE_FRAME_REQUEST)
    {
        //同一路FIFO的请求号单调递增。终结未消费时不能覆盖原记录。
        if (transfer->state != CAMERA_TRANSFER_EMPTY || message->command_id <= transfer->last_request ||
            message->stream_epoch != 0U || message->lease_id != 0U)
            return;
        transfer->last_request = message->command_id;
        transfer->reply = (AppMessage_t){.source = RUNTIME_TARGET_CAMERA, .command_id = message->command_id};
        transfer->admitted = admission;
        transfer->state = CAMERA_TRANSFER_REQUESTED;
        if (!admission)
            CameraFrame_Terminal(transfer, APP_MESSAGE_FRAME_OFFER, SYS_ERR_INVALID_STATE);
        return;
    }

    if (message->type == APP_MESSAGE_FRAME_CANCEL_REQUEST &&
        message->command_id == transfer->last_request &&
        message->stream_epoch == 0U && message->lease_id == 0U)
    {
        //已入下行FIFO的OFFER无法撤回，由消费者看到同一OFFER后按身份归还。
        if (transfer->state == CAMERA_TRANSFER_REQUESTED)
            CameraFrame_Terminal(transfer, APP_MESSAGE_FRAME_OFFER, SYS_ERR_INVALID_STATE);
        else if (transfer->state == CAMERA_TRANSFER_OFFER_PENDING)
        {
            transfer->reject_offer = true;
            transfer->state = CAMERA_TRANSFER_RELEASING;
        }
        return;
    }

    if (!CameraFrame_Matches(transfer, message))
        return;
    if (message->type == APP_MESSAGE_FRAME_RETURN && transfer->state == CAMERA_TRANSFER_PUBLISHED)
        transfer->state = CAMERA_TRANSFER_RELEASING;
    else if (message->type == APP_MESSAGE_RETURN_ACK_CONSUMED &&
        transfer->state == CAMERA_TRANSFER_WAIT_CONSUMED)
    {
        //保留退休高水位，重复消息不得命中下一请求。
        transfer->lease.raw = (CameraFrameLease_t){0};
        transfer->jpeg = false;
        transfer->reply = (AppMessage_t){.command_id = 0U};
        transfer->admitted = false;
        transfer->reject_offer = false;
        transfer->state = CAMERA_TRANSFER_EMPTY;
    }
}

//按采集格式借出帧，成功后保存租约并准备投递
static void CameraFrame_Acquire(CameraTransfer_t *transfer, CameraFrameConsumer_t consumer)
{
    SysResult_t result = SYS_ERR_NO_RESOURCE;

    //序号用于观测分发顺序，不使用任意地址或缓冲区号编码
    transfer->jpeg = s_camera.config != NULL &&
        s_camera.config->format == CAMERA_PIXEL_FORMAT_JPEG;
    if (s_frame_sequence != UINT32_MAX)
    {
        if (transfer->jpeg)
            result = CameraCapture_StreamAcquireJpeg(consumer, &transfer->lease.jpeg, &transfer->reply.frame);
        else
            result = CameraCapture_StreamAcquire(consumer, &transfer->lease.raw, &transfer->reply.frame);
    }
    if (result == SYS_ERR_BUSY)
        return;
    if (result != SYS_OK)
    {
        CameraFrame_Terminal(transfer, APP_MESSAGE_FRAME_OFFER, result);
        return;
    }

    transfer->reply.type = APP_MESSAGE_FRAME_OFFER;
    transfer->reply.stream_epoch = transfer->jpeg ? transfer->lease.jpeg.session : transfer->lease.raw.generation;
    transfer->reply.lease_id = transfer->jpeg ? transfer->lease.jpeg.id : transfer->lease.raw.lease_id;
    transfer->reply.frame_id = ++s_frame_sequence;
    transfer->reply.timestamp_ms = Platform_TimeGetMs();
    transfer->state = CAMERA_TRANSFER_OFFER_PENDING;
}

//按每轮一次推进，失败保留原物理身份和待提交回复
static SysResult_t CameraFrame_AdvanceSlot(CameraFrameConsumer_t consumer, bool admission, CameraFrameSend_t send)
{
    CameraTransfer_t *transfer = &s_transfers[consumer];
    SysResult_t result;

    if (transfer->state == CAMERA_TRANSFER_REQUESTED)
    {
        if (admission)
            CameraFrame_Acquire(transfer, consumer);
        else
            CameraFrame_Terminal(transfer, APP_MESSAGE_FRAME_OFFER, SYS_ERR_INVALID_STATE);
        return SYS_OK;
    }

    if (transfer->state == CAMERA_TRANSFER_OFFER_PENDING)
    {
        if (!admission)
        {
            //停止后只撤销从未公开的View，不撤销任何已发布租约
            transfer->reject_offer = true;
            transfer->state = CAMERA_TRANSFER_RELEASING;
        }
        else if (send(consumer, &transfer->reply) == SYS_OK)
            transfer->state = CAMERA_TRANSFER_PUBLISHED;
        return SYS_OK;
    }

    if (transfer->state == CAMERA_TRANSFER_RELEASING)
    {
        if (transfer->jpeg)
            result = CameraCapture_StreamReleaseJpeg(&transfer->lease.jpeg);
        else
            result = CameraCapture_StreamReleaseLease(&transfer->lease.raw);
        if (result != SYS_OK)
            return result;

        //取消未发送OFFER时，负向终结仍使用零租约身份
        if (transfer->reject_offer)
        {
            transfer->reply.stream_epoch = 0U;
            transfer->reply.lease_id = 0U;
            transfer->reply.frame_id = 0U;
            CameraFrame_Terminal(transfer, APP_MESSAGE_FRAME_OFFER, SYS_ERR_INVALID_STATE);
        }
        else
            CameraFrame_Terminal(transfer, APP_MESSAGE_RETURN_ACK, SYS_OK);
        return SYS_OK;
    }

    if (transfer->state == CAMERA_TRANSFER_TERMINAL_PENDING && send(consumer, &transfer->reply) == SYS_OK)
        transfer->state = CAMERA_TRANSFER_WAIT_CONSUMED;
    return SYS_OK;
}

//轮换扫描消费者，每个槽最多推进一次
static SysResult_t CameraFrame_Advance(bool admission, CameraFrameSend_t send)
{
    uint32_t offset;
    uint8_t scan_start = s_next_consumer;
    bool priority_advanced = false;
    SysResult_t first_error = SYS_OK;

    //初始优先USB，随后轮换起点。每个挂起的消费者每轮只推进一次。
    for (offset = 0U; offset < CAMERA_FRAME_CONSUMER_COUNT; ++offset)
    {
        CameraFrameConsumer_t consumer =
            (CameraFrameConsumer_t)((scan_start + offset) % CAMERA_FRAME_CONSUMER_COUNT);
        CameraTransferState_t before = s_transfers[consumer].state;
        SysResult_t result = CameraFrame_AdvanceSlot(consumer, admission && first_error == SYS_OK, send);

        //空转不改变优先级；首个成功借出的消费者把下一轮优先级交给后继。
        if (!priority_advanced && before == CAMERA_TRANSFER_REQUESTED &&
            s_transfers[consumer].state == CAMERA_TRANSFER_OFFER_PENDING)
        {
            s_next_consumer = (uint8_t)((consumer + 1U) % CAMERA_FRAME_CONSUMER_COUNT);
            priority_advanced = true;
        }
        if (result != SYS_OK && result != SYS_ERR_BUSY && first_error == SYS_OK)
            first_error = result;
    }
    return first_error;
}

//判断停机前受理的交接是否全部完成
static bool CameraFrame_Draining(void)
{
    uint32_t index;

    //只等待关闭前已受理的事务。关闭后拒绝的新请求不能拖延本次STOP。
    for (index = 0U; index < CAMERA_FRAME_CONSUMER_COUNT; ++index)
        if (s_transfers[index].admitted)
            return true;
    return false;
}

//推进帧消息消费及归还，关闭准入后继续排空
static SysResult_t CameraService_AdvanceFrames(bool admission)
{
    AppMessage_t message;
    uint32_t count;

    //原EventLoop仍只订阅服务命令。数据FIFO每轮有界处理并由轮询兜底。
    for (count = 0U; count < 4U && App_MessagesReceive(APP_CHANNEL_TO_CAMERA, &message); ++count)
        CameraFrame_Receive(CAMERA_FRAME_CONSUMER_USB, &message, admission);
    for (count = 0U; count < 4U && App_MessagesReceive(APP_CHANNEL_STORAGE_TO_CAMERA, &message); ++count)
        CameraFrame_Receive(CAMERA_FRAME_CONSUMER_STORAGE, &message, admission);

    return CameraFrame_Advance(admission, CameraFrame_Send);
}

//今后只由CameraTask调用。Device Init已在Framework完成，Media Init按本配置只成功一次
static SysResult_t CameraService_StartCapture(void)
{
    SysResult_t result;
    CameraCaptureStopFacts_t facts;

    if (s_camera.sensor == NULL)
        s_camera.sensor = (Ov5640Device_t *)Device_Find("ov5640");
    if (s_camera.sensor == NULL)
        return SYS_ERR_NOT_FOUND;

    if (!s_camera.sensor_configured)
    {
        result = s_camera.sensor->ops->configure(s_camera.sensor, s_camera.config);
        if (result != SYS_OK)
            return result;
        s_camera.sensor_configured = true;
    }
    if (!s_camera.media_initialized)
    {
        result = CameraCapture_Init(s_camera.config);
        if (result != SYS_OK)
            return result;
        s_camera.media_initialized = true;
    }
    if (!s_camera.sensor_started)
    {
        result = Device_Start(&s_camera.sensor->base);
        if (result != SYS_OK)
            return result;
        s_camera.sensor_started = true;
    }

    //记录启动前的停止序号，不能让上一轮停止事实满足当前停止请求
    result = CameraCapture_GetStopFacts(&facts);
    if (result != SYS_OK)
        return result;
    s_camera.stop_baseline = facts.attempt_sequence;
    result = CameraCapture_StartStream();
    if (result != SYS_ERR_BUSY)
        s_camera.capture_attempted = true;
    if (result == SYS_OK)
        s_camera.waiting_frame = true;
    return result;
}

//推进真实Media并刷新统计，ACTIVE期间不得借走待分发帧
static SysResult_t CameraService_ObserveMedia(void)
{
    CameraCaptureStats_t stats;
    SysResult_t result;

    CameraCapture_Process();
    result = CameraCapture_GetLastResult();
    if (result != SYS_OK)
        return result;
    result = CameraCapture_GetStats(&stats);
    if (result != SYS_OK)
        return result;
    s_camera.hardware.primary = stats.completed_frames;
    s_camera.hardware.detail = stats.dropped_frames;
    return SYS_OK;
}

//只有START首帧自检同步借出并立即归还，之后才开放Dispatcher
static SysResult_t CameraService_Sample(void)
{
    CameraFrame_t frame = {0};
    SysResult_t result = CameraService_ObserveMedia();
    bool valid;

    if (result != SYS_OK)
        return result;
    if (!CameraCapture_StreamGetFrame(&frame))
        return SYS_ERR_BUSY;

    s_camera.frame_held = true;
    s_camera.hardware.flags |= CAMERA_HW_FRAME_HELD;
    valid = frame.data != NULL && frame.size_bytes != 0U &&
        frame.width == s_camera.config->width && frame.height == s_camera.config->height &&
        frame.format == s_camera.config->format;
    if (valid && !CameraConfig_IsVariableLength(frame.format))
    {
        uint32_t bytes;
        valid = CameraConfig_GetFrameSize(s_camera.config, &bytes) == SYS_OK &&
            frame.size_bytes == bytes && frame.size_bytes2 == 0U;
    }
    result = CameraCapture_StreamReleaseFrame();
    if (result != SYS_OK)
        return result;
    s_camera.frame_held = false;
    s_camera.hardware.flags &= (uint16_t)~CAMERA_HW_FRAME_HELD;
    if (!valid)
        return SYS_ERR_PROTOCOL;

    //确认帧标志只在完整取得并归还后设置
    s_camera.hardware.flags |= CAMERA_HW_FRAME_SEEN;
    return SYS_OK;
}

//在当前采集实例中确认实际DMA Stop和无待归还后，才允许触碰传感器停止
static SysResult_t CameraService_StopMedia(void)
{
    CameraCaptureStopFacts_t facts;
    SysResult_t result;

    if (!s_camera.capture_attempted)
        return !s_camera.media_initialized || CameraCapture_IsStopped() ? SYS_OK : SYS_ERR_BUSY;
    if (!CameraCapture_IsStopped())
    {
        result = CameraCapture_Stop();
        if (result != SYS_OK)
            return result;
    }
    result = CameraCapture_GetStopFacts(&facts);
    if (result != SYS_OK)
        return result;
    if (!CameraCapture_IsStopped() || (facts.flags & CAMERA_STOP_DRAINING) != 0U)
        return SYS_ERR_BUSY;
    if (facts.attempt_sequence == s_camera.stop_baseline || facts.hardware_result != SYS_OK ||
        (facts.flags & (CAMERA_STOP_COMPLETE | CAMERA_STOP_ERROR)) != CAMERA_STOP_COMPLETE)
        return SYS_ERR_PROTOCOL;
    s_camera.capture_attempted = false;
    s_camera.waiting_frame = false;
    s_camera.hardware.flags |= CAMERA_HW_MEDIA_STOPPED;
    return SYS_OK;
}

//恢复只调用有真实所有权的原API；Device错误态没有公开复位契约
static SysResult_t CameraService_StopCapture(void)
{
    SysResult_t result;

    if (s_camera.frame_held)
    {
        result = CameraCapture_StreamReleaseFrame();
        if (result != SYS_OK)
            return result;
        s_camera.frame_held = false;
        s_camera.hardware.flags &= (uint16_t)~CAMERA_HW_FRAME_HELD;
    }
    result = CameraService_StopMedia();
    if (result != SYS_OK)
        return result;
    //物理停机与消费确认分别证明；未消费的已受理ACK不能提前退休。
    if (CameraFrame_Draining())
        return SYS_ERR_BUSY;

    if (s_camera.sensor != NULL && s_camera.sensor->base.state == DEVICE_STATE_ERROR)
    {
        s_camera.hardware.flags |= CAMERA_HW_DEVICE_ERROR;
        return SYS_ERR_INVALID_STATE;
    }
    if (s_camera.sensor_started)
    {
        result = Device_Stop(&s_camera.sensor->base);
        if (result != SYS_OK)
            return result;
        s_camera.sensor_started = false;
    }
    return SYS_OK;
}

//提交只登记意图；真实调用和终结都由后续Camera轮次完成
static SysResult_t CameraService_HardwareCommand(const AppV3ServiceCommand_t *command, SysResult_t result)
{
    AppV3HardwareState_t state = s_camera.hardware.state;

    if (result != SYS_OK || command->operation == CAMERA_SERVICE_HARDWARE_QUERY)
        return AppV3Mailbox_CompleteHardware(&s_camera.mailbox, command, result, &s_camera.hardware);
    if (command->operation == CAMERA_SERVICE_START)
    {
        if (state != APP_V3_HW_READY)
            result = state == APP_V3_HW_ACTIVE ? SYS_ERR_ALREADY_EXISTS : SYS_ERR_INVALID_STATE;
        else
        {
            s_camera.hardware = (AppV3HardwareReport_t){.state = APP_V3_HW_STARTING};
            s_camera.rollback_fault = SYS_OK;
        }
    }
    else if (state == APP_V3_HW_ACTIVE || state == APP_V3_HW_FAULT)
    {
        s_camera.hardware.state = APP_V3_HW_QUIESCING;
        s_camera.rollback_fault = SYS_OK;
    }
    else
        return AppV3Mailbox_CompleteHardware(&s_camera.mailbox, command,
            state == APP_V3_HW_READY ? SYS_OK : SYS_ERR_INVALID_STATE, &s_camera.hardware);

    if (result != SYS_OK)
        return AppV3Mailbox_CompleteHardware(&s_camera.mailbox, command, result, &s_camera.hardware);
    s_camera.request = (AppV3HardwareRequest_t){*command, Platform_TimeGetMs(), true};
    return SYS_OK;
}

//保存首错和最终硬件状态，再发布命令终结
static void CameraService_FinishHardware(SysResult_t result, AppV3HardwareState_t state)
{
    if (result != SYS_OK && s_camera.hardware.first_fault == SYS_OK)
        s_camera.hardware.first_fault = result;
    s_camera.hardware.state = state;
    s_camera.request.pending = false;
    //Mailbox在Control背压时保留同一终结消息，下一轮继续提交
    (void)AppV3Mailbox_CompleteHardware(&s_camera.mailbox, &s_camera.request.command, result, &s_camera.hardware);
}

//一次轮次最多推进一个操作阶段，忙等待有明确期限
uint32_t AppV3_CameraAdvance(void)
{
    SysResult_t result;
    uint32_t now;

    if (!App_MessagesIsOwner(RUNTIME_TARGET_CAMERA))
        return APP_EVENTS_WAIT_FOREVER;
    if (s_camera.hardware.state == APP_V3_HW_ACTIVE)
    {
        result = CameraService_ObserveMedia();
        if (result != SYS_OK && result != SYS_ERR_BUSY)
        {
            if (s_camera.hardware.first_fault == SYS_OK)
                s_camera.hardware.first_fault = result;
            s_camera.hardware.state = APP_V3_HW_FAULT;
        }
    }

    result = CameraService_AdvanceFrames(s_camera.hardware.state == APP_V3_HW_ACTIVE);
    if (result != SYS_OK)
    {
        //真实归还错误仍须留账并推进；ACTIVE立即关闭新的借出。
        if (s_camera.hardware.first_fault == SYS_OK)
            s_camera.hardware.first_fault = result;
        if (s_camera.hardware.state == APP_V3_HW_ACTIVE)
            s_camera.hardware.state = APP_V3_HW_FAULT;
    }
    if (s_camera.hardware.state == APP_V3_HW_ACTIVE || !s_camera.request.pending)
        return CAMERA_HW_POLL_MS;

    now = Platform_TimeGetMs();
    if (s_camera.hardware.state == APP_V3_HW_STARTING)
    {
        //等待首帧时才读取Media；启动成功返回不能直接完成命令
        result = now - s_camera.request.started_ms >= CAMERA_HW_TIMEOUT_MS ? SYS_ERR_TIMEOUT :
            (s_camera.waiting_frame ? CameraService_Sample() : CameraService_StartCapture());
        if (result == SYS_OK && (s_camera.hardware.flags & CAMERA_HW_FRAME_SEEN) != 0U)
            CameraService_FinishHardware(SYS_OK, APP_V3_HW_ACTIVE);
        if (result != SYS_OK && result != SYS_ERR_BUSY)
        {
            s_camera.rollback_fault = result;
            s_camera.hardware.first_fault = result;
            s_camera.hardware.state = APP_V3_HW_QUIESCING;
            s_camera.request.started_ms = now;
        }
        return CAMERA_HW_POLL_MS;
    }

    result = CameraService_StopCapture();
    if (result == SYS_ERR_BUSY && now - s_camera.request.started_ms < CAMERA_HW_TIMEOUT_MS)
        return CAMERA_HW_POLL_MS;
    if (result == SYS_ERR_BUSY)
        result = SYS_ERR_TIMEOUT;
    CameraService_FinishHardware(s_camera.rollback_fault != SYS_OK ? s_camera.rollback_fault : result,
        result == SYS_OK ? APP_V3_HW_READY : APP_V3_HW_FAULT);
    return CAMERA_HW_POLL_MS;
}

//一次可读回调只接收一条命令，Event Loop负责控制批次预算
static SysResult_t CameraService_Readable(AppV3Queue_t queue, void *argument)
{
    CameraServiceContext_t *context = &s_camera;
    AppV3ServiceCommand_t command = {0};
    SysResult_t result;
    (void)argument;

    //所属任务与队列都要匹配，禁止跨Service取消息
    if (!App_MessagesIsOwner(RUNTIME_TARGET_CAMERA) ||
        queue.index != context->mailbox.queue.index ||
        queue.generation != context->mailbox.queue.generation)
        return SYS_ERR_INVALID_STATE;

    if (context->request.pending)
        return SYS_ERR_BUSY;
    result = AppV3Mailbox_Receive(&context->mailbox, &command);
    if (command.command_id == 0U || (uint32_t)command.requester >= RUNTIME_TARGET_COUNT)
        return result;

    if (command.operation >= CAMERA_SERVICE_START && command.operation <= CAMERA_SERVICE_RECOVER)
        return CameraService_HardwareCommand(&command, result);

    if (result == SYS_OK)
        result = SYS_ERR_NOT_SUPPORTED;
    return AppV3Mailbox_Complete(&context->mailbox, &command, result, false, 0U);
}

//准备本服务的配置、队列与事件订阅，再由Registry发布Record
SysResult_t AppV3_CameraInit(const AppV3Context_t *context)
{
    SysResult_t result;

    if (!App_MessagesIsOwner(RUNTIME_TARGET_CAMERA))
        return SYS_ERR_INVALID_STATE;
    if (s_camera.config != NULL)
        return SYS_ERR_ALREADY_EXISTS;
    if (context == NULL || context->video == NULL)
        return SYS_ERR_INVALID_STATE;

    result = CameraCapture_ValidateStreamConfig(&context->video->camera);
    if (result != SYS_OK)
        return result;
    result = AppV3Mailbox_Init(&s_camera.mailbox, RUNTIME_TARGET_CAMERA, CameraService_Readable, &s_endpoint);
    if (result == SYS_OK)
    {
        s_camera.config = &context->video->camera;
        s_camera.hardware.state = APP_V3_HW_READY;
    }
    return result;
}

//Record发布失败时只回收未公开的队列和订阅
void AppV3_CameraDiscard(void)
{
    if (!App_MessagesIsOwner(RUNTIME_TARGET_CAMERA))
        return;
    AppV3Mailbox_Discard(&s_camera.mailbox, &s_endpoint);
    s_camera.config = NULL;
    s_camera.hardware.state = APP_V3_HW_UNINITIALIZED;
}

//公开API持有Record期间提交值类型消息，发送方不接触私有Context
SysResult_t CameraService_Request(uint32_t command_id, CameraServiceOperation_t operation, uint32_t argument)
{
    return AppV3Mailbox_Request(RUNTIME_TARGET_CAMERA, &s_endpoint, command_id, operation, argument);
}

//固定Registry指向本服务的实际线程入口
void AppV3_CameraRun(void *argument)
{
    AppV3_ServiceRun(argument, &s_camera.mailbox, AppV3_CameraAdvance);
}
