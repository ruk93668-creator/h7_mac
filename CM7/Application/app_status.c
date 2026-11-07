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

    if (!AppStatus_MemoryValid(status, sizeof(*status)))
        return SYS_ERR_INVALID_ARG;
    if (!App_EventsIsOwner(RUNTIME_TARGET_CAMERA))
        return SYS_ERR_INVALID_STATE;
    value.valid_mask = IPC_APP_CAMERA;
    value.camera_state = status->state;
    value.camera_fps_milli = status->fps_milli;
    value.captured_frames = status->captured_frames;
    value.dropped_frames = status->dropped_frames;
    value.camera_result = status->result;
    if (status->config_valid)
    {
        value.width = status->width;
        value.height = status->height;
        value.format = AppStatus_Format(status->format);
        if (value.format == IPC_IMAGE_UNKNOWN)
            return SYS_ERR_INVALID_ARG;
    }
    if (!IPC_Application_Validate(&value))
        return SYS_ERR_INVALID_ARG;

    //同一临界区更新字段、有效位和时间戳，ISR不参与写入
    taskENTER_CRITICAL();
    s_status.camera_state = value.camera_state;
    s_status.camera_fps_milli = value.camera_fps_milli;
    s_status.captured_frames = value.captured_frames;
    s_status.dropped_frames = value.dropped_frames;
    s_status.width = value.width;
    s_status.height = value.height;
    s_status.format = value.format;
    s_status.camera_result = value.camera_result;
    s_status.valid_mask |= IPC_APP_CAMERA;
    s_source_ticks[0] = Platform_TimeGetMs();
    s_source_expired[0] = false;
    taskEXIT_CRITICAL();
    return SYS_OK;
}

//提交USB完整记录
SysResult_t App_StatusPublishUsb(const AppUsbStatus_t *status)
{
    if (!AppStatus_MemoryValid(status, sizeof(*status)))
        return SYS_ERR_INVALID_ARG;
    if (!App_EventsIsOwner(RUNTIME_TARGET_USB))
        return SYS_ERR_INVALID_STATE;
    if (status->state > 2U || status->result > SYS_OK)
        return SYS_ERR_INVALID_ARG;
    taskENTER_CRITICAL();
    s_status.usb_state = status->state;
    s_status.usb_fps_milli = status->fps_milli;
    s_status.usb_completed_frames = status->completed_frames;
    s_status.usb_result = status->result;
    s_status.valid_mask |= IPC_APP_USB;
    s_source_ticks[1] = Platform_TimeGetMs();
    s_source_expired[1] = false;
    taskEXIT_CRITICAL();
    return SYS_OK;
}

//提交Control完整记录
SysResult_t App_StatusPublishControl(const AppControlStatus_t *status)
{
    if (!AppStatus_MemoryValid(status, sizeof(*status)))
        return SYS_ERR_INVALID_ARG;
    if (!App_EventsIsOwner(RUNTIME_TARGET_CONTROL))
        return SYS_ERR_INVALID_STATE;
    if (status->state > 6U || status->result > SYS_OK || status->last_fault > SYS_OK)
        return SYS_ERR_INVALID_ARG;
    taskENTER_CRITICAL();
    s_status.control_state = status->state;
    s_status.control_result = status->result;
    s_status.last_fault = status->last_fault;
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

    if (!AppStatus_MemoryValid(snapshot, sizeof(*snapshot)) ||
        !AppStatus_MemoryValid(sampled_tick, sizeof(*sampled_tick)))
        return SYS_ERR_INVALID_ARG;
    if ((uintptr_t)snapshot <= (uintptr_t)sampled_tick + sizeof(*sampled_tick) - 1U &&
        (uintptr_t)sampled_tick <= (uintptr_t)snapshot + sizeof(*snapshot) - 1U)
        return SYS_ERR_INVALID_ARG;
    if (!App_EventsIsOwner(RUNTIME_TARGET_IPC))
        return SYS_ERR_INVALID_STATE;
    taskENTER_CRITICAL();
    value = s_status;
    now = Platform_TimeGetMs();
    value.snapshot_id = ++s_snapshot_id;
    if ((value.valid_mask & IPC_APP_CAMERA) != 0U)
        value.camera_age_ms = AppStatus_Age(0U, now);
    if ((value.valid_mask & IPC_APP_USB) != 0U)
        value.usb_age_ms = AppStatus_Age(1U, now);
    if ((value.valid_mask & IPC_APP_CONTROL) != 0U)
        value.control_age_ms = AppStatus_Age(2U, now);
    g_app_status_debug = value;
    taskEXIT_CRITICAL();
    *snapshot = value;
    *sampled_tick = now;
    return SYS_OK;
}

//拒绝空指针、未对齐、整数溢出及共享窗口别名
static bool AppStatus_MemoryValid(const void *memory, uint32_t bytes)
{
    uintptr_t address = (uintptr_t)memory;
    if (memory == NULL || (address & 3U) != 0U ||
        address > UINTPTR_MAX - (bytes - 1U))
        return false;
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

    if (age >= IPC_APPLICATION_FRESH_MS)
        s_source_expired[source] = true;
    return s_source_expired[source] ? UINT32_MAX : age;
}

//保留只需要业务副本的本地读取接口
SysResult_t App_StatusGetSnapshot(IpcApplicationStatus_t *snapshot)
{
    uint32_t sampled_tick;
    return App_StatusGetSnapshotAt(snapshot, &sampled_tick);
}
