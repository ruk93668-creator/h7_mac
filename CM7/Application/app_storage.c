#include "app_storage.h"
#include "app_events.h"
#include "video_config.h"
#include "platform_time.h"
#include "jpeg_frame.h"
#include "mjpeg_avi.h"
#include "bsp_sdram.h"
#include "camera_mjpeg_fifo.h"
#include "stm32h7xx.h"
#include <string.h>

//副本槽位的唯一所有者随状态转移，绝不覆盖WRITING
#define SLOT_FREE 0U
#define SLOT_COPYING 1U
#define SLOT_QUEUED 2U
#define SLOT_WRITING 3U
#define STORAGE_COMMANDS 4U

//状态唯一保存在私有上下文，Debug中的state只是观察副本
typedef enum { STORAGE_IDLE = 0, STORAGE_VIDEO, STORAGE_SNAPSHOT, STORAGE_DRAINING, STORAGE_FAULT } StorageState_t;

typedef char StoragePoolBounds[(APP_STORAGE_POOL_BASE >= CAMERA_MJPEG_REGION_END &&
    APP_STORAGE_POOL_END <= MJPEG_AVI_INDEX_BASE &&
    MJPEG_AVI_INDEX_BASE + MJPEG_AVI_INDEX_BYTES <= BSP_SDRAM_END &&
    APP_STORAGE_SLOT_STRIDE % 32U == 0U) ? 1 : -1];

//凭据只索引独立副本，Storage不保存Media指针
typedef struct
{
    uint32_t state;
    uint32_t generation;
    uint32_t session;
    uint32_t size;
    uint32_t crc;
    AppFrameMeta_t meta;
} StorageSlot_t;

//固定槽位、帧队列及命令队列都不依赖内核堆
static volatile StorageSlot_t s_slots[APP_STORAGE_SLOT_COUNT];
typedef struct
{
    uint32_t slot;
    uint32_t generation;
    uint32_t session;
} StorageQueueItem_t;

//队列凭据和槽位当前内容分开，过期消息不能冒充新副本
static volatile StorageQueueItem_t s_queue[APP_STORAGE_SLOT_COUNT];
//队列头和数量只在短临界区提交
static volatile uint32_t s_head, s_count;
//命令与帧队列分开，满帧队列不能吞掉停止请求
static volatile uint32_t s_commands[STORAGE_COMMANDS], s_command_head, s_command_count;
//保存冷启动策略，不向下层传播Application全局变量
static AppStorageConfig_t s_config;
//状态是唯一决策来源，Debug只作外部观察
static StorageState_t s_state;
//达到容量或分段限制后只回收余下副本
static bool s_limit_reached;
//保存接收入口、收尾意图及单次抓拍是否已取得副本
static volatile bool s_prepared, s_accepting, s_stop_pending, s_snapshot_taken;
//会话号防止旧副本混入新录像，文件编号不覆盖旧文件
static volatile uint32_t s_session, s_copying, s_last_offer, s_next_file = 1U;
//记录采样起点和文件系统是否仍由本任务挂载
static volatile bool s_offer_seen, s_mounted;
//当前用途互斥：录像或单张抓拍
static volatile AppStorageMode_t s_mode;
//当前文件句柄只由Storage任务操作
static StorageFile_t s_file;
//单个AVI的容器状态，不保存Media对象
static MjpegAvi_t s_avi;
//跟踪分段、总字节预算及已扣除待写索引的空间余额
static uint32_t s_segment_tick, s_total_bytes, s_segments, s_free_budget;
//分别跟踪实际写帧速率、输入超时和周期同步
static uint32_t s_fps_tick, s_fps_frames, s_sync_tick;

//Camera发布输入进展，Storage读取；多字段仍由临界区和DMB保护
static volatile uint32_t s_input_tick;

//查询接口只返回Storage任务发布的完整副本
static AppStorageStatus_t s_status_snapshot;

//回读复用小型CPU缓冲，实际IDMA仍走原32KiB适配缓冲
static uint8_t s_readback[512] __attribute__((aligned(32)));

//诊断没有参与USB或Media所有权判断
volatile AppStorageStatus_t g_storage_debug;

//在短临界区发布完整诊断，查询不读取半更新结构
static void Storage_PublishStatus(void);

//仅普通任务环境可发保存命令，不在ISR执行文件操作
static bool Storage_Context(void);

//短临界区只投递命令，不持锁等待队列或SD
static SysResult_t Storage_Request(uint32_t command);

//计算副本地址，固定步长已由编译期范围检查保护
static uint8_t *Storage_Data(uint32_t slot);

//事件分发与实际文件操作分离，不在状态退出时隐式释放外部资源
static void Storage_HandleEvent(uint32_t command);

//空闲状态才允许挂载，命令被受理不等于文件已创建
static void Storage_OnStart(uint32_t command);

//停止只关闭新副本入口，完成需要后续排空和文件关闭
static void Storage_OnStop(void);

//故障恢复由存储任务显式执行，不自动反复重试
static void Storage_OnRecover(void);

//所有运行状态只能通过本入口提交
static void Storage_Transition(StorageState_t state);

//创建不覆盖的8.3文件，冲突搜索最多128次
static SysResult_t Storage_OpenNew(bool video);

//完整写入当前数据，显式检查部分写入
static SysResult_t Storage_Write(const uint8_t *data, uint32_t size);

//回读抓拍文件，校验真实长度、CRC和JPEG首尾
static SysResult_t Storage_Snapshot(const StorageSlot_t *slot, const uint8_t *data);

//关闭当前AVI，索引或同步失败时保留真实错误
static SysResult_t Storage_CloseAvi(void);

//结束会话并保留清理结果，不把失败转换为保存成功
static void Storage_Finish(SysResult_t result);

//把首个文件错误转为存储侧故障，Camera和USB继续运行
static void Storage_Fail(SysResult_t result);

//只回收已验证的WRITING凭据，防止重复或过期归还
static bool Storage_Release(uint32_t slot, uint32_t generation);

//准备静态状态，不打开SD卡
SysResult_t App_StoragePrepare(const AppStorageConfig_t *config)
{
    if (s_prepared || config == NULL)
        return SYS_ERR_INVALID_STATE;
    if (config->record_fps == 0U || config->record_fps > 60U || config->segment_bytes < 131072U ||
        config->segment_bytes > 0x10000000UL || config->segment_ms == 0U || config->segment_ms > 3600000U ||
        config->total_bytes < config->segment_bytes || config->total_bytes > 0x40000000UL ||
        config->reserve_bytes > 0x40000000UL || config->max_segments == 0U || config->max_segments > 1000U)
        return SYS_ERR_INVALID_ARG;
    s_config = *config;
    __DMB();
    s_prepared = true;
    g_storage_debug.ready = 1U;
    Storage_PublishStatus();
    return SYS_OK;
}

//异步受理录像请求
SysResult_t App_StorageStartRecording(void) { return Storage_Request(1U); }

//异步受理单张抓拍，不自动改变JPEG采集配置
SysResult_t App_StorageRequestSnapshot(void) { return Storage_Request(2U); }

//立即关闭新副本入口，再由Storage任务确认排空和关闭
SysResult_t App_StorageStop(void)
{
    SysResult_t result = Storage_Request(3U);
    uint32_t mask;
    if (result == SYS_OK)
    {
        mask = __get_PRIMASK();
        __disable_irq();
        __DMB();
        s_accepting = false;
        __DMB();
        __set_PRIMASK(mask);
    }
    return result;
}

//显式请求恢复，不隐式格式化或删除文件
SysResult_t App_StorageRecover(void) { return Storage_Request(4U); }

//只提供观察副本，外部不得据此归还图像
SysResult_t App_StorageGetStatus(AppStorageStatus_t *status)
{
    uint32_t mask;
    if (status == NULL || !Storage_Context())
        return SYS_ERR_INVALID_ARG;
    mask = __get_PRIMASK();
    __disable_irq();
    __DMB();
    *status = s_status_snapshot;
    __DMB();
    __set_PRIMASK(mask);
    return SYS_OK;
}

//存储采样使用独立目标，不依赖Windows是否开始收流
bool App_StorageWantsFrame(uint32_t now)
{
    bool wanted;
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    __DMB();
    wanted = s_prepared && s_accepting && !s_snapshot_taken &&
        (!s_offer_seen || s_mode == APP_STORAGE_SNAPSHOT ||
         (uint32_t)(now - s_last_offer) >= (1000U + s_config.record_fps - 1U) / s_config.record_fps);
    __DMB();
    __set_PRIMASK(mask);
    return wanted;
}

//只在Camera持有原帧时复制，槽位满直接拒绝
SysResult_t App_StorageOfferFrame(const CameraFrame_t *frame, const AppFrameMeta_t *meta)
{
    uint32_t mask, slot, generation, session, first, start, elapsed;
    uint8_t *data;
    SysResult_t result;
    if (!App_EventsIsOwner(RUNTIME_TARGET_CAMERA) || meta == NULL || !s_prepared)
        return SYS_ERR_INVALID_STATE;
    if (!App_StorageWantsFrame(meta->timestamp_ms))
        return SYS_ERR_BUSY;
    if (frame == NULL || meta->frame_id == 0U || meta->stream_epoch == 0U ||
        meta->width != frame->width || meta->height != frame->height ||
        frame->size_bytes > APP_STORAGE_FRAME_LIMIT || frame->size_bytes == 0U)
    {
        g_storage_debug.dropped_frames++;
        return SYS_ERR_INVALID_ARG;
    }
    mask = __get_PRIMASK();
    __disable_irq();
    __DMB();
    for (slot = 0U; slot < APP_STORAGE_SLOT_COUNT; slot++)
        if (s_slots[slot].state == SLOT_FREE && s_slots[slot].generation != UINT32_MAX)
            break;
    if (!s_accepting || slot == APP_STORAGE_SLOT_COUNT)
    {
        g_storage_debug.dropped_frames++;
        __DMB();
        __set_PRIMASK(mask);
        return SYS_ERR_NO_RESOURCE;
    }
    s_slots[slot].state = SLOT_COPYING;
    generation = ++s_slots[slot].generation;
    session = s_session;
    s_copying++;
    __DMB();
    __set_PRIMASK(mask);

    //复制和校验可被USB抢占，不在临界区扫描JPEG
    start = Platform_TimeGetMs();
    result = JpegFrame_Validate(frame);
    data = Storage_Data(slot);
    if (result == SYS_OK)
    {
        first = frame->size_bytes - frame->size_bytes2;
        memcpy(data, frame->data, first);
        if (frame->size_bytes2 != 0U)
            memcpy(data + first, frame->data2, frame->size_bytes2);
        s_slots[slot].size = frame->size_bytes;
        s_slots[slot].crc = ~JpegFrame_Crc32(data, frame->size_bytes, UINT32_MAX);
        s_slots[slot].meta = *meta;
        s_slots[slot].session = session;
    }
    elapsed = Platform_TimeGetMs() - start;
    mask = __get_PRIMASK();
    __disable_irq();
    __DMB();
    s_copying--;
    if (elapsed > g_storage_debug.max_copy_ms)
        g_storage_debug.max_copy_ms = elapsed;
    if (result == SYS_OK && s_accepting && session == s_session && s_count < APP_STORAGE_SLOT_COUNT &&
        s_slots[slot].generation == generation)
    {
        s_slots[slot].state = SLOT_QUEUED;
        s_queue[(s_head + s_count) % APP_STORAGE_SLOT_COUNT].slot = slot;
        s_queue[(s_head + s_count) % APP_STORAGE_SLOT_COUNT].generation = generation;
        s_queue[(s_head + s_count) % APP_STORAGE_SLOT_COUNT].session = session;
        s_count++;
        g_storage_debug.queue_depth = s_count;
        if (s_count > g_storage_debug.queue_high_water)
            g_storage_debug.queue_high_water = s_count;
        s_last_offer = meta->timestamp_ms;
        s_input_tick = Platform_TimeGetMs();
        s_offer_seen = true;
        if (s_mode == APP_STORAGE_SNAPSHOT)
        {
            s_snapshot_taken = true;
            s_accepting = false;
        }
    }
    else
    {
        s_slots[slot].state = SLOT_FREE;
        g_storage_debug.dropped_frames++;
        if (result == SYS_OK)
            result = SYS_ERR_INVALID_STATE;
    }
    __DMB();
    __set_PRIMASK(mask);
    (void)RuntimeEvent_Post(RUNTIME_TARGET_STORAGE, RUNTIME_EVENT_WAKE);
    return result;
}

//Storage任务每轮执行有限工作，文件系统调用只在这里及私有函数发生
void App_StorageProcess(void)
{
    uint32_t command = 0U, mask, slot = APP_STORAGE_SLOT_COUNT, generation = 0U, start, elapsed;
    uint32_t limit, amount;
    StorageSlot_t item;
    StorageQueueItem_t queued;
    bool invalid_queue = false;
    SysResult_t result = SYS_OK;
    if (!App_EventsIsOwner(RUNTIME_TARGET_STORAGE) || !s_prepared)
        return;
    mask = __get_PRIMASK();
    __disable_irq();
    __DMB();
    if (s_command_count != 0U)
    {
        command = s_commands[s_command_head];
        s_command_head = (s_command_head + 1U) % STORAGE_COMMANDS;
        s_command_count--;
    }
    __DMB();
    __set_PRIMASK(mask);
    if (command != 0U)
        Storage_HandleEvent(command);
    mask = __get_PRIMASK();
    __disable_irq();
    __DMB();
    if (s_count != 0U)
    {
        queued = s_queue[s_head];
        s_head = (s_head + 1U) % APP_STORAGE_SLOT_COUNT;
        s_count--;
        g_storage_debug.queue_depth = s_count;

        //槽位、代号和会话必须全部匹配，才允许取得WRITING所有权
        if (queued.slot >= APP_STORAGE_SLOT_COUNT ||
            s_slots[queued.slot].state != SLOT_QUEUED ||
            s_slots[queued.slot].generation != queued.generation ||
            s_slots[queued.slot].session != queued.session)
            invalid_queue = true;
        else
        {
            slot = queued.slot;
            s_slots[slot].state = SLOT_WRITING;
            item = s_slots[slot];
            generation = item.generation;
        }
    }
    __DMB();
    __set_PRIMASK(mask);
    if (invalid_queue)
        Storage_Fail(SYS_ERR_PROTOCOL);
    if (slot != APP_STORAGE_SLOT_COUNT)
    {
        start = Platform_TimeGetMs();
        if (item.session != s_session || !s_mounted || s_state == STORAGE_FAULT)
            result = SYS_ERR_INVALID_STATE;
        else if (s_mode == APP_STORAGE_SNAPSHOT)
            result = Storage_Snapshot(&item, Storage_Data(slot));
        else
        {
            amount = item.size + 24U + (item.size & 1U);
            if (amount > s_free_budget || s_free_budget - amount < s_config.reserve_bytes)
            {

                //预留空间不足时正常封尾已有完整帧，不故意写到文件系统报满
                s_stop_pending = true;
                s_limit_reached = true;
                g_storage_debug.stop_reason = 3U;
            }
            //总预算耗尽时只排空并丢弃待写副本，不创建空的下一段
            if (result == SYS_OK && s_file == 0U && !s_limit_reached &&
                (s_segments >= s_config.max_segments ||
                 s_total_bytes + 224U + amount + 8U > s_config.total_bytes))
            {
                s_stop_pending = true;
                s_limit_reached = true;
                g_storage_debug.stop_reason = 2U;
            }
            if (result == SYS_OK && s_file == 0U && !s_limit_reached)
                result = Storage_OpenNew(true);
            limit = s_config.segment_bytes;
            if (limit > s_config.total_bytes - s_total_bytes)
                limit = s_config.total_bytes - s_total_bytes;
            if (result == SYS_OK && !s_limit_reached && (s_avi.frames >= MJPEG_AVI_MAX_FRAMES ||
                (s_avi.frames != 0U && (uint32_t)(item.meta.timestamp_ms - s_segment_tick) >= s_config.segment_ms) ||
                s_avi.position + amount + 8U + s_avi.frames * 16U > limit))
            {
                result = Storage_CloseAvi();
                if (result == SYS_OK && s_segments < s_config.max_segments &&
                    s_total_bytes + 224U + amount + 8U <= s_config.total_bytes)
                    result = Storage_OpenNew(true);
                else if (result == SYS_OK)
                {
                    s_stop_pending = true;
                    s_limit_reached = true;
                    g_storage_debug.stop_reason = 2U;
                }
            }
            if (s_limit_reached)
                g_storage_debug.dropped_frames++;
            if (result == SYS_OK && !s_limit_reached)
            {
                if (s_avi.frames == 0U)
                    s_segment_tick = item.meta.timestamp_ms;
                limit = s_config.segment_bytes;
                if (limit > s_config.total_bytes - s_total_bytes)
                    limit = s_config.total_bytes - s_total_bytes;
                result = MjpegAvi_Append(&s_avi, Storage_Data(slot), item.size, item.meta.timestamp_ms, limit);
                if (result == SYS_OK)
                {
                    s_free_budget -= amount;
                    g_storage_debug.saved_frames++;

                    //周期同步在低优先级存储任务进行，不修改AVI已接收帧的所有权
                    if ((uint32_t)(Platform_TimeGetMs() - s_sync_tick) >= 1000U)
                    {
                        result = StorageFS_Sync(s_file);
                        s_sync_tick = Platform_TimeGetMs();
                    }
                }
            }
        }
        elapsed = Platform_TimeGetMs() - start;
        if (elapsed > g_storage_debug.max_write_ms)
            g_storage_debug.max_write_ms = elapsed;
        g_storage_debug.last_size = item.size;
        g_storage_debug.last_crc = item.crc;
        g_storage_debug.frame_id = item.meta.frame_id;
        g_storage_debug.timestamp_ms = item.meta.timestamp_ms;
        g_storage_debug.stream_epoch = item.meta.stream_epoch;
        (void)Storage_Release(slot, generation);
        if (result != SYS_OK)
            Storage_Fail(result);
        else if (s_mode == APP_STORAGE_SNAPSHOT)
        {
            g_storage_debug.stop_reason = 5U;
            Storage_Finish(SYS_OK);

            //JPG只有验证、关闭和卸载均成功才完成本次抓拍
            if (s_state == STORAGE_IDLE)
            {
                g_storage_debug.saved_files++;
                g_storage_debug.saved_frames++;
            }
        }
    }
    mask = __get_PRIMASK();
    __disable_irq();
    __DMB();
    if (s_stop_pending)
        s_accepting = false;
    command = s_stop_pending && s_count == 0U && s_copying == 0U;
    __DMB();
    __set_PRIMASK(mask);
    if (command != 0U && s_state != STORAGE_FAULT)
        Storage_Finish(SYS_OK);
    start = Platform_TimeGetMs();

    //采集长期没有交付完整帧时结束存储等待，不影响USB和Camera恢复路径
    if (s_mounted && s_accepting && s_count == 0U && s_copying == 0U &&
        (uint32_t)(start - s_input_tick) >= 5000U)
        Storage_Fail(SYS_ERR_TIMEOUT);
    elapsed = start - s_fps_tick;
    if (elapsed >= 1000U)
    {
        amount = g_storage_debug.saved_frames - s_fps_frames;
        g_storage_debug.measured_fps_milli = amount <= 4294U ? amount * 1000000U / elapsed : 0U;
        s_fps_tick = start;
        s_fps_frames = g_storage_debug.saved_frames;
    }
    Storage_PublishStatus();

    //通知位可以合并，队列仍有工作时自行保留后续处理机会
    if (s_count != 0U || s_command_count != 0U)
        (void)RuntimeEvent_Post(RUNTIME_TARGET_STORAGE, RUNTIME_EVENT_WAKE);
}

//只接受调度器内普通任务，不允许中断触发同步文件操作
static bool Storage_Context(void)
{
    return __get_IPSR() == 0U && __get_PRIMASK() == 0U && __get_BASEPRI() == 0U;
}

//命令队列只保存编号，不携带外部对象或文件句柄
static SysResult_t Storage_Request(uint32_t command)
{
    uint32_t mask;
    if (!Storage_Context() || !s_prepared)
        return SYS_ERR_INVALID_STATE;
    if ((command == 1U || command == 2U) &&
        (VideoConfig_GetDefault()->camera.format != CAMERA_PIXEL_FORMAT_JPEG ||
         VideoConfig_GetDefault()->output == VIDEO_OUTPUT_USB_TEST))
    {
        mask = __get_PRIMASK();
        __disable_irq();
        __DMB();
        g_storage_debug.rejected_requests++;
        __DMB();
        __set_PRIMASK(mask);
        return SYS_ERR_NOT_SUPPORTED;
    }
    mask = __get_PRIMASK();
    __disable_irq();
    __DMB();
    if (s_command_count >= STORAGE_COMMANDS)
    {
        g_storage_debug.rejected_requests++;
        __DMB();
        __set_PRIMASK(mask);
        return SYS_ERR_NO_RESOURCE;
    }
    s_commands[(s_command_head + s_command_count) % STORAGE_COMMANDS] = command;
    s_command_count++;
    g_storage_debug.requested++;
    __DMB();
    __set_PRIMASK(mask);
    (void)RuntimeEvent_Post(RUNTIME_TARGET_STORAGE, RUNTIME_EVENT_WAKE);
    return SYS_OK;
}

//固定副本地址不与JPEG接收FIFO或索引区重叠
static uint8_t *Storage_Data(uint32_t slot)
{
    return (uint8_t *)(APP_STORAGE_POOL_BASE + slot * APP_STORAGE_SLOT_STRIDE);
}

//只在Storage任务消费命令，开始请求默认不自动发出
//统一分发保存事件，状态决策在各处理函数中完成
static void Storage_HandleEvent(uint32_t command)
{
    switch (command)
    {
        case 1U:
        case 2U:
            Storage_OnStart(command);
            break;

        case 3U:
            Storage_OnStop();
            break;

        case 4U:
            Storage_OnRecover();
            break;

        default:
            g_storage_debug.result = SYS_ERR_INVALID_ARG;
            break;
    }
}

//同一会话只选录像或抓拍，不能在活动时直接切换用途
static void Storage_OnStart(uint32_t command)
{
    SysResult_t result;
    uint64_t total, free_bytes;
    uint32_t mask;
    if (s_mounted || s_state != STORAGE_IDLE || s_session == UINT32_MAX)
    {
        g_storage_debug.rejected_requests++;
        g_storage_debug.result = SYS_ERR_BUSY;
        return;
    }
    result = StorageFS_Mount(true, 0U);
    if (result != SYS_OK)
    {
        Storage_Fail(result);
        return;
    }
    s_mounted = true;
    result = StorageFS_GetSpace(&total, &free_bytes);
    if (result != SYS_OK || free_bytes <= s_config.reserve_bytes + APP_STORAGE_FRAME_LIMIT + 256U)
    {
        Storage_Fail(result != SYS_OK ? result : SYS_ERR_NO_RESOURCE);
        return;
    }
    s_free_budget = free_bytes > UINT32_MAX ? UINT32_MAX : (uint32_t)free_bytes;
    s_total_bytes = 0U;
    s_segments = 0U;
    s_input_tick = Platform_TimeGetMs();
    s_sync_tick = s_input_tick;
    mask = __get_PRIMASK();
    __disable_irq();
    __DMB();
    s_session++;
    s_mode = command == 1U ? APP_STORAGE_VIDEO : APP_STORAGE_SNAPSHOT;
    s_accepting = true;
    s_stop_pending = false;
    s_limit_reached = false;
    s_snapshot_taken = false;
    s_offer_seen = false;
    Storage_Transition(command == 1U ? STORAGE_VIDEO : STORAGE_SNAPSHOT);
    g_storage_debug.mode = s_mode;
    g_storage_debug.stop_reason = 0U;
    g_storage_debug.session = s_session;
    g_storage_debug.result = SYS_OK;
    g_storage_debug.cleanup_result = SYS_OK;
    __DMB();
    __set_PRIMASK(mask);
}

//故障状态要求显式恢复，重复Stop不能伪装故障已经消失
static void Storage_OnStop(void)
{
    if (s_state == STORAGE_FAULT)
        return;
    s_accepting = false;
    g_storage_debug.stop_reason = 1U;
    s_stop_pending = s_mounted;
    if (s_mounted)
        Storage_Transition(STORAGE_DRAINING);
}

//保留失败的挂载租约直到底层确认恢复完成
static void Storage_OnRecover(void)
{
    StorageFsDiagnostics_t diagnostics;
    SysResult_t result;
    uint32_t mask, i;
    if (s_copying != 0U || s_count != 0U || s_state != STORAGE_FAULT)
    {
        g_storage_debug.result = SYS_ERR_BUSY;
        return;
    }
    result = StorageFS_GetDiagnostics(&diagnostics);
    if (result == SYS_OK)
    {
        switch (diagnostics.state)
        {
            case 3U:

                //08C只允许仍持有真实故障租约的任务执行Recover
                result = StorageFS_Recover();
                break;

            case 2U:

                //普通操作失败但卷仍挂载时，先重试关闭与卸载
                if (s_file != 0U)
                {
                    result = StorageFS_Close(s_file);
                    if (result == SYS_OK)
                        s_file = 0U;
                }
                if (result == SYS_OK)
                    result = StorageFS_Unmount();
                break;

            case 1U:

                //READY说明资源已安全归还，仅清除应用层故障
                result = SYS_OK;
                break;

            default:
                result = SYS_ERR_INVALID_STATE;
                break;
        }
    }
    g_storage_debug.result = result;
    if (result == SYS_OK)
    {
        s_mounted = false;
        s_file = 0U;
        mask = __get_PRIMASK();
        __disable_irq();
        __DMB();

        //此时没有复制、队列或文件I/O，回收独立副本，不触碰Media/USB
        for (i = 0U; i < APP_STORAGE_SLOT_COUNT; i++)
            s_slots[i].state = SLOT_FREE;
        __DMB();
        __set_PRIMASK(mask);
        g_storage_debug.cleanup_result = SYS_OK;
        Storage_Transition(STORAGE_IDLE);
    }
    (void)StorageFS_GetDiagnostics((StorageFsDiagnostics_t *)&g_storage_debug.filesystem);
}

//固定8.3命名且有界搜索，绝不覆盖或删除已有文件
static SysResult_t Storage_OpenNew(bool video)
{
    char name[17];
    uint32_t tries, i, value;
    SysResult_t result;
    for (tries = 0U; tries < 128U && s_next_file <= 99999U; tries++)
    {
        memcpy(name, video ? "0:/VID00000.AVI" : "0:/IMG00000.JPG", 15U);
        name[15] = 0;
        value = s_next_file++;
        for (i = 0U; i < 5U; i++)
        {
            name[10U - i] = (char)('0' + value % 10U);
            value /= 10U;
        }
        result = StorageFS_Open(name, STORAGE_FS_OPEN_WRITE | STORAGE_FS_OPEN_CREATE_NEW, &s_file);
        if (result == SYS_ERR_ALREADY_EXISTS)
            continue;
        if (result != SYS_OK)
            return result;
        g_storage_debug.file_number = s_next_file - 1U;
        if (video)
        {
            result = MjpegAvi_Begin(&s_avi, s_file, VideoConfig_GetDefault()->camera.width,
                VideoConfig_GetDefault()->camera.height, (uint16_t)s_config.record_fps);
            if (result != SYS_OK)
                return result;
            s_segments++;
            if (s_free_budget >= 224U)
                s_free_budget -= 224U;
        }
        return SYS_OK;
    }
    return SYS_ERR_NO_RESOURCE;
}

//文件写入依次走原32KiB块设备缓冲，不把整帧交给IDMA
static SysResult_t Storage_Write(const uint8_t *data, uint32_t size)
{
    uint32_t done = 0U, amount, n;
    SysResult_t result;
    while (done < size)
    {
        amount = size - done;
        if (amount > 32768U)
            amount = 32768U;
        result = StorageFS_Write(s_file, data + done, amount, &n);
        if (result != SYS_OK || n != amount)
            return result != SYS_OK ? result : SYS_ERR_NO_RESOURCE;
        done += n;
    }
    return SYS_OK;
}

//抓拍关闭后重新打开并核对，不把写缓存比较当落卡验收
static SysResult_t Storage_Snapshot(const StorageSlot_t *slot, const uint8_t *data)
{
    char name[17];
    uint32_t value, i, n, size, done = 0U, crc = UINT32_MAX;
    SysResult_t result = Storage_OpenNew(false);
    if (result == SYS_OK)
        result = Storage_Write(data, slot->size);
    if (result == SYS_OK)
        result = StorageFS_Sync(s_file);
    if (result == SYS_OK)
    {
        result = StorageFS_Close(s_file);
        if (result == SYS_OK)
            s_file = 0U;
    }
    if (result != SYS_OK)
        return result;
    memcpy(name, "0:/IMG00000.JPG", 15U);
    name[15] = 0;
    value = g_storage_debug.file_number;
    for (i = 0U; i < 5U; i++)
    {
        name[10U - i] = (char)('0' + value % 10U);
        value /= 10U;
    }
    result = StorageFS_Open(name, STORAGE_FS_OPEN_READ, &s_file);
    if (result == SYS_OK)
        result = StorageFS_GetSize(s_file, &size);
    if (result == SYS_OK && size != slot->size)
        result = SYS_ERR_PROTOCOL;
    while (result == SYS_OK && done < slot->size)
    {
        size = slot->size - done;
        if (size > sizeof(s_readback))
            size = sizeof(s_readback);
        result = StorageFS_Read(s_file, s_readback, size, &n);
        if (result == SYS_OK && n != size)
            result = SYS_ERR_PROTOCOL;
        if (result == SYS_OK)
        {
            if (memcmp(s_readback, data + done, n) != 0)
                result = SYS_ERR_PROTOCOL;
            crc = JpegFrame_Crc32(s_readback, n, crc);
            done += n;
        }
    }
    g_storage_debug.read_crc = ~crc;
    if (result == SYS_OK && ~crc != slot->crc)
        result = SYS_ERR_PROTOCOL;
    if (result == SYS_OK)
    {
        result = StorageFS_Close(s_file);
        if (result == SYS_OK)
        {
            s_file = 0U;
        }
    }
    return result;
}

//AVI关闭成功才统计完整文件，不在失败文件上伪造完成头
static SysResult_t Storage_CloseAvi(void)
{
    SysResult_t result;
    if (s_file == 0U)
        return SYS_OK;
    result = MjpegAvi_Finish(&s_avi);
    if (result == SYS_OK)
        result = StorageFS_Close(s_file);
    if (result == SYS_OK)
    {
        s_file = 0U;
        s_total_bytes += s_avi.position + 8U + s_avi.frames * 16U;
        if (s_free_budget >= 8U)
            s_free_budget -= 8U;
        g_storage_debug.saved_files++;
    }
    return result;
}

//正常结束和错误清理分别记录，失败不影响Camera或USB
static void Storage_Finish(SysResult_t result)
{
    SysResult_t cleanup = SYS_OK, step;
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    __DMB();
    s_accepting = false;
    __DMB();
    __set_PRIMASK(mask);
    if (s_file != 0U)
    {
        cleanup = s_mode == APP_STORAGE_VIDEO && result == SYS_OK ? Storage_CloseAvi() : StorageFS_Close(s_file);
        if (cleanup == SYS_OK)
            s_file = 0U;
    }
    if (s_mounted)
    {
        step = StorageFS_Unmount();
        if (cleanup == SYS_OK)
            cleanup = step;
        if (step == SYS_OK)
            s_mounted = false;
    }
    (void)StorageFS_GetDiagnostics((StorageFsDiagnostics_t *)&g_storage_debug.filesystem);
    s_stop_pending = false;
    g_storage_debug.result = result;
    g_storage_debug.cleanup_result = cleanup;
    if (cleanup != SYS_OK && result == SYS_OK)
        g_storage_debug.failures++;
    Storage_Transition(result == SYS_OK && cleanup == SYS_OK ? STORAGE_IDLE : STORAGE_FAULT);
    g_storage_debug.mode = APP_STORAGE_OFF;
}

//拒绝新副本，队列里旧副本会在后续轮次逐个安全回收
static void Storage_Fail(SysResult_t result)
{
    uint32_t mask = __get_PRIMASK();
    uint32_t slot;
    StorageQueueItem_t queued;
    __disable_irq();
    __DMB();
    s_accepting = false;

    //只丢弃尚未写入的副本，COPYING由原生产者检查会话后回收
    while (s_count != 0U)
    {
        queued = s_queue[s_head];
        slot = queued.slot;
        s_head = (s_head + 1U) % APP_STORAGE_SLOT_COUNT;
        s_count--;
        if (slot < APP_STORAGE_SLOT_COUNT && s_slots[slot].state == SLOT_QUEUED &&
            s_slots[slot].generation == queued.generation && s_slots[slot].session == queued.session)
            s_slots[slot].state = SLOT_FREE;
        g_storage_debug.dropped_frames++;
    }
    g_storage_debug.queue_depth = 0U;
    __DMB();
    __set_PRIMASK(mask);
    g_storage_debug.failures++;
    g_storage_debug.stop_reason = result == SYS_ERR_TIMEOUT ? 6U : 4U;
    Storage_Finish(result);
}

//槽位代号必须与本次WRITING一致，旧归还不能释放新副本
static bool Storage_Release(uint32_t slot, uint32_t generation)
{
    uint32_t mask = __get_PRIMASK();
    bool released = false;
    __disable_irq();
    __DMB();
    if (slot < APP_STORAGE_SLOT_COUNT && s_slots[slot].state == SLOT_WRITING &&
        s_slots[slot].generation == generation)
    {
        s_slots[slot].state = SLOT_FREE;
        released = true;
    }
    __DMB();
    __set_PRIMASK(mask);
    return released;
}

//状态提交集中在这里，Debug副本不作为内部决策来源
static void Storage_Transition(StorageState_t state)
{
    s_state = state;
    g_storage_debug.state = state;
}

//完整快照只在这里提交，Camera统计写入也不能打断这次复制
static void Storage_PublishStatus(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    __DMB();
    s_status_snapshot = g_storage_debug;
    __DMB();
    __set_PRIMASK(mask);
}
