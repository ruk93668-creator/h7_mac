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

    //拒绝无效快照，避免污染已经缓存的完整数据
    if (cache == NULL || !IPC_Application_Validate(application))
        return;

    //首次收到空业务载荷时继续等待，不把它当成有效样本
    if (!cache->has_sample && application->valid_mask == 0U)
        return;

    //只有新采样编号才更新接收时间，重复读取不能延长数据寿命
    if (!cache->has_sample || application->snapshot_id != cache->application.snapshot_id)
    {
        cache->application = *application;
        cache->received_tick = now;
        cache->has_sample = true;
        cache->expired = false;
    }

    //旧编号长期不变时锁存过期，避免Tick整圈回绕后重新变新鲜
    else if ((uint32_t)(now - cache->received_tick) >= IPC_APPLICATION_FRESH_MS)
        cache->expired = true;
}

//提供缓存副本和按本地Tick计算的新鲜度
SysResult_t IPC_Application_GetView(const IpcApplicationCache_t *cache,
    uint32_t now, uint32_t peer_state, SysResult_t read_result, IpcApplicationView_t *view)
{
    IpcApplicationView_t value = {0};
    uint32_t elapsed;

    //先检查输入输出指针，防止访问空缓存或写入空地址
    if (cache == NULL || view == NULL)
        return SYS_ERR_INVALID_ARG;

    //保留通信状态和最近读取结果，业务新鲜度另行判断
    value.peer_state = peer_state;
    value.read_result = read_result;

    //没有有效样本时返回空视图，以最大年龄明确表示未知
    if (!cache->has_sample)
    {
        value.receive_age_ms = UINT32_MAX;

        //没有样本时只交付通信状态，来源有效位和新鲜位保持零
        *view = value;
        return SYS_OK;
    }

    //复制完整缓存，后续只修改本地视图，不改变缓存原始年龄
    value.application = cache->application;
    value.valid_mask = cache->application.valid_mask;

    //只计算本核接收后经过的时间，不比较两核绝对Tick
    elapsed = (uint32_t)(now - cache->received_tick);
    value.receive_age_ms = elapsed;

    //Camera有效时累计等待时间，过期数据不能保持旧年龄
    if ((value.valid_mask & IPC_APP_CAMERA) != 0U)
        value.application.camera_age_ms = IpcApplication_AddAge(value.application.camera_age_ms, elapsed);

    //USB独立累计年龄，不能借Camera的新数据延长有效期
    if ((value.valid_mask & IPC_APP_USB) != 0U)
        value.application.usb_age_ms = IpcApplication_AddAge(value.application.usb_age_ms, elapsed);

    //Control独立累计年龄，避免用旧控制状态冒充当前状态
    if ((value.valid_mask & IPC_APP_CONTROL) != 0U)
        value.application.control_age_ms = IpcApplication_AddAge(value.application.control_age_ms, elapsed);

    //缓存未过期且对端在线时才检查新鲜度；短暂BUSY可沿用未过期快照
    if (!cache->expired &&
        peer_state == IPC_PEER_ONLINE &&
        (read_result == SYS_OK || read_result == SYS_ERR_BUSY) &&
        elapsed < IPC_APPLICATION_FRESH_MS)
    {

        //Camera自身年龄未超限，才允许显示为实时数据
        if (value.application.camera_age_ms < IPC_APPLICATION_FRESH_MS)
            value.fresh_mask |= IPC_APP_CAMERA;

        //USB独立判断新鲜度，不能仅凭对端在线就认定FPS实时
        if (value.application.usb_age_ms < IPC_APPLICATION_FRESH_MS)
            value.fresh_mask |= IPC_APP_USB;

        //Control独立判断新鲜度，防止旧状态误导上层
        if (value.application.control_age_ms < IPC_APPLICATION_FRESH_MS)
            value.fresh_mask |= IPC_APP_CONTROL;
    }

    //移除未声明有效的来源位，新鲜数据必须首先有效
    value.fresh_mask &= value.valid_mask;

    //所有判断完成后一次性交付视图，避免调用者取得半成品
    *view = value;
    return SYS_OK;
}

//饱和累计数据年龄
static uint32_t IpcApplication_AddAge(uint32_t age, uint32_t elapsed)
{

    //年龄溢出时保持最大值，避免旧数据绕回较小年龄
    return age > UINT32_MAX - elapsed ? UINT32_MAX : age + elapsed;
}
