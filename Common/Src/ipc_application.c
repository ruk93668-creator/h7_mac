#include "ipc_application.h"
#include "ipc_runtime.h"

//饱和相加，长时间陈旧不能因加法回绕变新鲜
static uint32_t IpcApplication_AddAge(uint32_t age, uint32_t elapsed);

//校验业务字段和来源位图
bool IPC_Application_Validate(const IpcApplicationStatus_t *a)
{
    if (a == NULL || (a->valid_mask & ~IPC_APP_ALL) != 0U)
        return false;
    if ((a->valid_mask & IPC_APP_CAMERA) != 0U)
    {
        if (a->camera_state > 2U || a->camera_result > SYS_OK ||
            a->format > IPC_IMAGE_JPEG || a->width > UINT16_MAX || a->height > UINT16_MAX)
            return false;
        if (a->format == IPC_IMAGE_UNKNOWN)
        {
            if (a->width != 0U || a->height != 0U || a->camera_state == 1U)
                return false;
        }
        else if (a->width == 0U || a->height == 0U ||
            (a->format == IPC_IMAGE_YUY2 && (a->width & 1U) != 0U))
            return false;
    }
    else if (a->camera_state != 0U || a->camera_fps_milli != 0U ||
        a->captured_frames != 0U || a->dropped_frames != 0U || a->width != 0U ||
        a->height != 0U || a->format != 0U || a->camera_result != 0 || a->camera_age_ms != 0U)
        return false;
    if ((a->valid_mask & IPC_APP_USB) != 0U)
    {
        if (a->usb_state > 2U || a->usb_result > SYS_OK)
            return false;
    }
    else if (a->usb_state != 0U || a->usb_fps_milli != 0U ||
        a->usb_completed_frames != 0U || a->usb_result != 0 || a->usb_age_ms != 0U)
        return false;
    if ((a->valid_mask & IPC_APP_CONTROL) != 0U)
    {
        if (a->control_state > 6U || a->control_result > SYS_OK || a->last_fault > SYS_OK)
            return false;
    }
    else if (a->control_state != 0U || a->control_result != 0 ||
        a->last_fault != 0 || a->control_age_ms != 0U)
        return false;
    return true;
}

//显式检查字段，不把结构体强制转换为整数数组
bool IPC_Application_IsEmpty(const IpcApplicationStatus_t *a)
{
    return a != NULL &&
        a->valid_mask == 0 &&
        a->camera_state == 0 &&
        a->usb_state == 0 &&
        a->control_state == 0 &&
        a->camera_fps_milli == 0 &&
        a->usb_fps_milli == 0 &&
        a->captured_frames == 0 &&
        a->dropped_frames == 0 &&
        a->usb_completed_frames == 0 &&
        a->width == 0 &&
        a->height == 0 &&
        a->format == 0 &&
        a->last_fault == 0 &&
        a->camera_result == 0 &&
        a->usb_result == 0 &&
        a->control_result == 0 &&
        a->camera_age_ms == 0 &&
        a->usb_age_ms == 0 &&
        a->control_age_ms == 0 &&
        a->snapshot_id == 0;
}

//只在业务编号出现新值时记录接收时刻
void IPC_Application_Observe(IpcApplicationCache_t *cache,
    const IpcApplicationStatus_t *application, uint32_t now)
{
    if (cache == NULL || !IPC_Application_Validate(application))
        return;
    if (!cache->has_sample && application->valid_mask == 0U)
        return;
    if (!cache->has_sample || application->snapshot_id != cache->application.snapshot_id)
    {
        cache->application = *application;
        cache->received_tick = now;
        cache->has_sample = true;
        cache->expired = false;
    }
    else if ((uint32_t)(now - cache->received_tick) >= IPC_APPLICATION_FRESH_MS)
        cache->expired = true;
}

//提供缓存副本和按本地Tick计算的新鲜度
SysResult_t IPC_Application_GetView(const IpcApplicationCache_t *cache,
    uint32_t now, uint32_t peer_state, SysResult_t read_result, IpcApplicationView_t *view)
{
    IpcApplicationView_t value = {0};
    uint32_t elapsed;

    if (cache == NULL || view == NULL)
        return SYS_ERR_INVALID_ARG;
    value.peer_state = peer_state;
    value.read_result = read_result;
    if (!cache->has_sample)
    {
        value.receive_age_ms = UINT32_MAX;
        *view = value;
        return SYS_OK;
    }
    value.application = cache->application;
    value.valid_mask = cache->application.valid_mask;
    elapsed = (uint32_t)(now - cache->received_tick);
    value.receive_age_ms = elapsed;

    //双方Tick不比较，只有发送时的年龄加本地接收后经过时间
    if ((value.valid_mask & IPC_APP_CAMERA) != 0U)
        value.application.camera_age_ms = IpcApplication_AddAge(value.application.camera_age_ms, elapsed);
    if ((value.valid_mask & IPC_APP_USB) != 0U)
        value.application.usb_age_ms = IpcApplication_AddAge(value.application.usb_age_ms, elapsed);
    if ((value.valid_mask & IPC_APP_CONTROL) != 0U)
        value.application.control_age_ms = IpcApplication_AddAge(value.application.control_age_ms, elapsed);
    if (!cache->expired && peer_state == IPC_PEER_ONLINE && (read_result == SYS_OK || read_result == SYS_ERR_BUSY) &&
        elapsed < IPC_APPLICATION_FRESH_MS)
    {
        if (value.application.camera_age_ms < IPC_APPLICATION_FRESH_MS)
            value.fresh_mask |= IPC_APP_CAMERA;
        if (value.application.usb_age_ms < IPC_APPLICATION_FRESH_MS)
            value.fresh_mask |= IPC_APP_USB;
        if (value.application.control_age_ms < IPC_APPLICATION_FRESH_MS)
            value.fresh_mask |= IPC_APP_CONTROL;
    }
    value.fresh_mask &= value.valid_mask;
    *view = value;
    return SYS_OK;
}

//饱和累计数据年龄
static uint32_t IpcApplication_AddAge(uint32_t age, uint32_t elapsed)
{
    return age > UINT32_MAX - elapsed ? UINT32_MAX : age + elapsed;
}
