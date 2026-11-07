#include "app_status.h"
#include "app_events.h"
#include "platform_time.h"
#include "ipc_config.h"
#include <stddef.h>

//所有来源共用短临界区，IPC读取者不能取得半更新记录
static IpcApplicationStatus_t s_status;

//分别记录三个任务最后提交的本地时间
static uint32_t s_source_ticks[3];

//来源长时间停更后保持陈旧，只有所属任务重新提交才能清除
static bool s_source_expired[3];

//记录汇总采样编号，允许无符号回绕
static uint32_t s_snapshot_id;

//提供IPC实际采样结果，区别于正在变化的各任务调试变量
volatile IpcApplicationStatus_t g_app_status_debug;

//检查普通本地参数，不允许借接口覆盖共享窗口
static bool AppStatus_MemoryValid(const void *memory, uint32_t bytes);

//显式映射Camera枚举，避免把枚举对象直接送入共享协议
static uint32_t AppStatus_Format(CameraPixelFormat_t format);

//在临界区计算来源年龄，超时后饱和避免完整Tick回绕使旧记录变新鲜
static uint32_t AppStatus_Age(uint32_t source, uint32_t now);

//提交Camera完整记录
SysResult_t App_StatusPublishCamera(const AppCameraStatus_t *status)
{
    IpcApplicationStatus_t value = {0};

    //先检查本核输入内存，避免非法访问或共享区别名
    if (!AppStatus_MemoryValid(status, sizeof(*status)))
        return SYS_ERR_INVALID_ARG;

    //只允许所属任务提交自己的状态，防止多任务抢写同一来源
    if (!App_EventsIsOwner(RUNTIME_TARGET_CAMERA))
        return SYS_ERR_INVALID_STATE;

    //先构造本地Camera记录，校验成功前不修改共享的本核汇总
    value.valid_mask = IPC_APP_CAMERA;
    value.camera_state = status->state;
    value.camera_fps_milli = status->fps_milli;
    value.captured_frames = status->captured_frames;
    value.dropped_frames = status->dropped_frames;
    value.camera_result = status->result;

    //只有实际配置有效时才报告分辨率和格式，不能用默认值冒充
    if (status->config_valid)
    {
        value.width = status->width;
        value.height = status->height;
        value.format = AppStatus_Format(status->format);

        //拒绝无法映射的Camera格式，避免两套枚举值被误解
        if (value.format == IPC_IMAGE_UNKNOWN)
            return SYS_ERR_INVALID_ARG;
    }

    //按跨核协议验证完整Camera记录后再提交
    if (!IPC_Application_Validate(&value))
        return SYS_ERR_INVALID_ARG;

    //将本来源的字段、有效位和采样时间一起提交，避免混读
    taskENTER_CRITICAL();
    s_status.camera_state = value.camera_state;
    s_status.camera_fps_milli = value.camera_fps_milli;
    s_status.captured_frames = value.captured_frames;
    s_status.dropped_frames = value.dropped_frames;
    s_status.width = value.width;
    s_status.height = value.height;
    s_status.format = value.format;
    s_status.camera_result = value.camera_result;

    //记录该来源已经发布，并重新建立它自己的新鲜度基线
    s_status.valid_mask |= IPC_APP_CAMERA;
    s_source_ticks[0] = Platform_TimeGetMs();
    s_source_expired[0] = false;
    taskEXIT_CRITICAL();
    return SYS_OK;
}

//提交USB完整记录
SysResult_t App_StatusPublishUsb(const AppUsbStatus_t *status)
{

    //先检查本核输入内存，避免非法访问或共享区别名
    if (!AppStatus_MemoryValid(status, sizeof(*status)))
        return SYS_ERR_INVALID_ARG;

    //只允许所属任务提交自己的状态，防止多任务抢写同一来源
    if (!App_EventsIsOwner(RUNTIME_TARGET_USB))
        return SYS_ERR_INVALID_STATE;

    //拒绝未知状态和正数错误码，保持跨核字段语义一致
    if (status->state > 2U || status->result > SYS_OK)
        return SYS_ERR_INVALID_ARG;

    //将本来源的字段、有效位和采样时间一起提交，避免混读
    taskENTER_CRITICAL();
    s_status.usb_state = status->state;
    s_status.usb_fps_milli = status->fps_milli;
    s_status.usb_completed_frames = status->completed_frames;
    s_status.usb_result = status->result;

    //记录该来源已经发布，并重新建立它自己的新鲜度基线
    s_status.valid_mask |= IPC_APP_USB;
    s_source_ticks[1] = Platform_TimeGetMs();
    s_source_expired[1] = false;
    taskEXIT_CRITICAL();
    return SYS_OK;
}

//提交Control完整记录
SysResult_t App_StatusPublishControl(const AppControlStatus_t *status)
{

    //先检查本核输入内存，避免非法访问或共享区别名
    if (!AppStatus_MemoryValid(status, sizeof(*status)))
        return SYS_ERR_INVALID_ARG;

    //只允许所属任务提交自己的状态，防止多任务抢写同一来源
    if (!App_EventsIsOwner(RUNTIME_TARGET_CONTROL))
        return SYS_ERR_INVALID_STATE;

    //拒绝未知状态和正数错误码，保持跨核字段语义一致
    if (status->state > 6U || status->result > SYS_OK || status->last_fault > SYS_OK)
        return SYS_ERR_INVALID_ARG;

    //将本来源的字段、有效位和采样时间一起提交，避免混读
    taskENTER_CRITICAL();
    s_status.control_state = status->state;
    s_status.control_result = status->result;
    s_status.last_fault = status->last_fault;

    //记录该来源已经发布，并重新建立它自己的新鲜度基线
    s_status.valid_mask |= IPC_APP_CONTROL;
    s_source_ticks[2] = Platform_TimeGetMs();
    s_source_expired[2] = false;
    taskEXIT_CRITICAL();
    return SYS_OK;
}

//在一个一致性边界读取三份完整记录
SysResult_t App_StatusGetSnapshotAt(IpcApplicationStatus_t *snapshot, uint32_t *sampled_tick)
{
    IpcApplicationStatus_t value;
    uint32_t now;

    //两个输出参数都必须可写，避免只返回数据而丢失配套采样时刻
    if (!AppStatus_MemoryValid(snapshot, sizeof(*snapshot)) ||
        !AppStatus_MemoryValid(sampled_tick, sizeof(*sampled_tick)))
        return SYS_ERR_INVALID_ARG;

    //两个输出区不能重叠，防止写入时间戳破坏业务快照
    if ((uintptr_t)snapshot <= (uintptr_t)sampled_tick + sizeof(*sampled_tick) - 1U &&
        (uintptr_t)sampled_tick <= (uintptr_t)snapshot + sizeof(*snapshot) - 1U)
        return SYS_ERR_INVALID_ARG;

    //汇总只由唯一IPC任务读取并编号，避免采样序号被多任务推进
    if (!App_EventsIsOwner(RUNTIME_TARGET_IPC))
        return SYS_ERR_INVALID_STATE;
    taskENTER_CRITICAL();

    //在同一临界区复制三份记录并采时，保证字段和年龄相互对应
    value = s_status;
    now = Platform_TimeGetMs();
    value.snapshot_id = ++s_snapshot_id;

    //只对已发布的Camera来源计算年龄，未发布字段保持零
    if ((value.valid_mask & IPC_APP_CAMERA) != 0U)
        value.camera_age_ms = AppStatus_Age(0U, now);

    //USB使用自己的最后发布时间，不能借其他任务更新续期
    if ((value.valid_mask & IPC_APP_USB) != 0U)
        value.usb_age_ms = AppStatus_Age(1U, now);

    //Control独立计算年龄，停止更新时可被CM4识别为陈旧
    if ((value.valid_mask & IPC_APP_CONTROL) != 0U)
        value.control_age_ms = AppStatus_Age(2U, now);

    //保留本次真实采样内容，便于与CM4收到的数据核对
    g_app_status_debug = value;
    taskEXIT_CRITICAL();

    //离开临界区后交付完整副本及同一采样时刻
    *snapshot = value;
    *sampled_tick = now;
    return SYS_OK;
}

//拒绝空指针、未对齐、整数溢出及共享窗口别名
static bool AppStatus_MemoryValid(const void *memory, uint32_t bytes)
{
    uintptr_t address = (uintptr_t)memory;

    //先排除空指针、未对齐和地址加法溢出
    if (memory == NULL || (address & 3U) != 0U ||
        address > UINTPTR_MAX - (bytes - 1U))
        return false;

    //拒绝与整个共享窗口重叠，防止本地接口绕过跨核所有权
    return !(address <= IPC_SHARED_BASE + IPC_SHARED_WINDOW_BYTES - 1U &&
        IPC_SHARED_BASE <= address + bytes - 1U);
}

//只接受已定义的Camera像素格式
static uint32_t AppStatus_Format(CameraPixelFormat_t format)
{
    switch (format)
    {
        case CAMERA_PIXEL_FORMAT_Y8: return IPC_IMAGE_Y8;
        case CAMERA_PIXEL_FORMAT_RGB565: return IPC_IMAGE_RGB565;
        case CAMERA_PIXEL_FORMAT_YUV422: return IPC_IMAGE_YUY2;
        case CAMERA_PIXEL_FORMAT_JPEG: return IPC_IMAGE_JPEG;
        default: return IPC_IMAGE_UNKNOWN;
    }
}

//一旦来源超过有效期限，就保持饱和直到该来源重新发布
static uint32_t AppStatus_Age(uint32_t source, uint32_t now)
{
    uint32_t age = (uint32_t)(now - s_source_ticks[source]);

    //达到有效期后锁存陈旧，不能因Tick完整回绕重新变新鲜
    if (age >= IPC_APPLICATION_FRESH_MS)
        s_source_expired[source] = true;

    //只有来源任务再次发布，才能清除陈旧标记
    return s_source_expired[source] ? UINT32_MAX : age;
}

//保留只需要业务副本的本地读取接口
SysResult_t App_StatusGetSnapshot(IpcApplicationStatus_t *snapshot)
{
    uint32_t sampled_tick;
    return App_StatusGetSnapshotAt(snapshot, &sampled_tick);
}
