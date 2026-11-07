#include "ipc_application.h"
#include "ipc_runtime.h"

//饱和相加，长时间陈旧不能因加法回绕变新鲜
static uint32_t IpcApplication_AddAge(uint32_t age, uint32_t elapsed);

//校验业务字段和来源位图
bool IPC_Application_Validate(const IpcApplicationStatus_t *a)
{
    //拒绝空数据和未知来源位，避免无效访问或误解协议
    if (a == NULL || (a->valid_mask & ~IPC_APP_ALL) != 0U)
        return false;

    //声明Camera有效后，才按Camera记录检查字段关系
    if ((a->valid_mask & IPC_APP_CAMERA) != 0U)
    {

        //拒绝非法状态、正数结果、未知格式和越界宽高
        if (a->camera_state > 2U || a->camera_result > SYS_OK ||
            a->format > IPC_IMAGE_JPEG || a->width > UINT16_MAX || a->height > UINT16_MAX)
            return false;

        //配置未知时不能声明分辨率，也不能声称Camera正在运行
        if (a->format == IPC_IMAGE_UNKNOWN)
        {
            if (a->width != 0U || a->height != 0U || a->camera_state == 1U)
                return false;
        }

        //已知格式必须有尺寸，YUY2要求每两个像素组成一组
        else if (a->width == 0U || a->height == 0U ||
            (a->format == IPC_IMAGE_YUY2 && (a->width & 1U) != 0U))
            return false;
    }

    //未声明Camera有效时必须全零，避免残留数据混入协议
    else if (a->camera_state != 0U || a->camera_fps_milli != 0U ||
        a->captured_frames != 0U || a->dropped_frames != 0U || a->width != 0U ||
        a->height != 0U || a->format != 0U || a->camera_result != 0 || a->camera_age_ms != 0U)
        return false;

    //声明USB有效后，检查其任务状态和操作结果
    if ((a->valid_mask & IPC_APP_USB) != 0U)
    {

        //拒绝未知USB状态，保持成功为0、失败为负数的约定
        if (a->usb_state > 2U || a->usb_result > SYS_OK)
            return false;
    }

    //USB未声明有效时不能携带旧状态、计数或时间信息
    else if (a->usb_state != 0U || a->usb_fps_milli != 0U ||
        a->usb_completed_frames != 0U || a->usb_result != 0 || a->usb_age_ms != 0U)
        return false;

    //声明Control有效后，检查协调状态和故障结果
    if ((a->valid_mask & IPC_APP_CONTROL) != 0U)
    {

        //拒绝未知协调状态和不符合返回码约定的正数结果
        if (a->control_state > 6U || a->control_result > SYS_OK || a->last_fault > SYS_OK)
            return false;
    }

    //Control未声明有效时，状态与故障记录都必须清零
    else if (a->control_state != 0U || a->control_result != 0 ||
        a->last_fault != 0 || a->control_age_ms != 0U)
        return false;

    //字段合法且来源一致，不代表数据新鲜或业务健康
    return true;
}

//显式检查字段，不把结构体强制转换为整数数组
bool IPC_Application_IsEmpty(const IpcApplicationStatus_t *a)
{
    //空指针不能视为空载荷，先保证后续字段访问安全
    return a != NULL &&

        //不声明任何业务来源，避免CM4发布CM7专有业务记录
        a->valid_mask == 0 &&

        //任务状态必须清零，不能残留旧运行状态
        a->camera_state == 0 &&
        a->usb_state == 0 &&
        a->control_state == 0 &&

        //空载荷不能携带历史采集或USB帧率
        a->camera_fps_milli == 0 &&
        a->usb_fps_milli == 0 &&

        //完成和丢帧计数必须清零，避免被误认为真实统计
        a->captured_frames == 0 &&
        a->dropped_frames == 0 &&
        a->usb_completed_frames == 0 &&

        //不声明图像配置，宽高和格式都应为零
        a->width == 0 &&
        a->height == 0 &&
        a->format == 0 &&

        //故障和操作结果必须清零，避免携带其他核的业务诊断
        a->last_fault == 0 &&
        a->camera_result == 0 &&
        a->usb_result == 0 &&
        a->control_result == 0 &&

        //没有业务记录，就不应携带来源年龄
        a->camera_age_ms == 0 &&
        a->usb_age_ms == 0 &&
        a->control_age_ms == 0 &&

        //采样编号也必须清零，确保整份载荷为空
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
