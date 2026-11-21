#include "camera_service.h"
#include "../../Tasks/service_tasks.h"
#include "camera_capture.h"
#include "ov5640_device.h"
#include "platform_time.h"


typedef struct
{
    AppV3ServiceMailbox_t mailbox;
    const CameraConfig_t *config;
    uint32_t pattern;
    bool simulated_running;
    AppV3HardwareReport_t hardware;
    AppV3HardwareRequest_t request;
    Ov5640Device_t *sensor;
    uint32_t stop_baseline;
    SysResult_t rollback_fault;
    bool sensor_configured;
    bool sensor_started;
    bool media_initialized;
    bool capture_attempted;
    bool waiting_frame;
    bool frame_held;
} CameraServiceContext_t;

typedef SysResult_t (*CameraServiceHandler_t)(CameraServiceContext_t *context,
    const AppV3ServiceCommand_t *command);

//只有所属任务修改私有状态。发布后API只读取单独保存的队列入口
static CameraServiceContext_t s_camera;
static AppV3Queue_t s_endpoint;

//只改变模拟状态，保持实际设备状态口独立
static SysResult_t CameraService_Start(CameraServiceContext_t *context,
    const AppV3ServiceCommand_t *command)
{
    (void)command;
    if (context->simulated_running)
        return SYS_ERR_INVALID_STATE;
    context->simulated_running = true;
    return SYS_OK;
}

//模拟停止不会访问硬件
static SysResult_t CameraService_Stop(CameraServiceContext_t *context,
    const AppV3ServiceCommand_t *command)
{
    (void)command;
    if (!context->simulated_running)
        return SYS_ERR_INVALID_STATE;
    context->simulated_running = false;
    return SYS_OK;
}

//校验本服务的模拟参数，失败时保留原值
static SysResult_t CameraService_Parameter(CameraServiceContext_t *context,
    const AppV3ServiceCommand_t *command)
{
    if (command->argument == 0U || command->argument > 255U)
        return SYS_ERR_INVALID_ARG;
    context->pattern = command->argument;
    return SYS_OK;
}

//完成消息携带当前模拟状态，不公开Context地址
static SysResult_t CameraService_Query(CameraServiceContext_t *context,
    const AppV3ServiceCommand_t *command)
{
    (void)context;
    (void)command;
    return SYS_OK;
}

//故障注入只生成软件FAULT
static SysResult_t CameraService_Fail(CameraServiceContext_t *context,
    const AppV3ServiceCommand_t *command)
{
    (void)context;
    (void)command;
    return SYS_ERR_IO;
}

static const CameraServiceHandler_t s_handlers[CAMERA_SERVICE_OP_COUNT] =
{
    [CAMERA_SERVICE_MOCK_START] = CameraService_Start,
    [CAMERA_SERVICE_MOCK_STOP] = CameraService_Stop,
    [CAMERA_SERVICE_SET_PARAMETER] = CameraService_Parameter,
    [CAMERA_SERVICE_QUERY] = CameraService_Query,
    [CAMERA_SERVICE_MOCK_FAIL] = CameraService_Fail
};

#define CAMERA_HW_POLL_MS 10U
#define CAMERA_HW_TIMEOUT_MS 3000U
#define CAMERA_HW_FRAME_SEEN 1U
#define CAMERA_HW_FRAME_HELD 2U
#define CAMERA_HW_MEDIA_STOPPED 4U
#define CAMERA_HW_DEVICE_ERROR 8U

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

    //记录启动前的停止序号，不能让上一轮停止事实满足本轮STOP
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

//仅检查本任务借到的完整帧，然后立刻归还，任何图像指针均不进入消息
static SysResult_t CameraService_Sample(void)
{
    CameraFrame_t frame = {0};
    CameraCaptureStats_t stats;
    SysResult_t result;
    bool valid;

    CameraCapture_Process();
    result = CameraCapture_GetLastResult();
    if (result != SYS_OK)
        return result;
    //即使本轮没有可取帧，也更新Media的完成和丢帧统计
    result = CameraCapture_GetStats(&stats);
    if (result != SYS_OK)
        return result;
    s_camera.hardware.primary = stats.completed_frames;
    s_camera.hardware.detail = stats.dropped_frames;
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
        result = CameraService_Sample();
        if (result != SYS_OK && result != SYS_ERR_BUSY)
        {
            s_camera.hardware.first_fault = result;
            s_camera.hardware.state = APP_V3_HW_FAULT;
        }
        return CAMERA_HW_POLL_MS;
    }
    if (!s_camera.request.pending)
        return APP_EVENTS_WAIT_FOREVER;

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

    //通过分发表执行合法命令；重复、迟到及非法请求只生成FAULT
    if (result == SYS_OK)
    {
        if (command.operation >= CAMERA_SERVICE_OP_COUNT || s_handlers[command.operation] == NULL)
            result = SYS_ERR_NOT_SUPPORTED;
        else
            result = s_handlers[command.operation](context, &command);
    }
    return AppV3Mailbox_Complete(&context->mailbox, &command, result,
        context->simulated_running, context->pattern);
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
