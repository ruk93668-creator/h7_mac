#include "app_storage.h"
#include "app_events.h"
#include "video_config.h"
#include "platform_time.h"
#include "jpeg_frame.h"
#include "mjpeg_avi.h"
#include "camera_mjpeg_fifo.h"
#include "platform_sync.h"
#include <string.h>

//帧租借槽位的唯一所有者随状态转移，绝不覆盖WRITING
#define SLOT_FREE 0U
#define SLOT_RESERVING 1U
#define SLOT_RETURN_PENDING 4U
#define SLOT_RETURNING 5U
#define SLOT_QUEUED 2U
#define SLOT_WRITING 3U
#define STORAGE_COMMANDS 4U

//状态唯一保存在私有上下文，Debug只保存观察值
typedef enum { STORAGE_IDLE = 0, STORAGE_VIDEO, STORAGE_SNAPSHOT, STORAGE_DRAINING, STORAGE_FAULT } StorageState_t;

//槽位保存原帧描述与SD凭据，Camera确认归还后才可复用
typedef struct
{
    uint32_t state;
    uint32_t generation;
    uint32_t session;
    uint32_t size;
    uint32_t crc;
    AppFrameMeta_t meta;

    //原帧地址在SD归还确认前保持有效，不分配图像副本
    CameraFrame_t frame;
    CameraMjpegLease_t lease;
} StorageSlot_t;

//固定槽位、帧队列及命令队列都不依赖内核堆
static volatile StorageSlot_t s_slots[APP_STORAGE_SLOT_COUNT];
typedef struct
{
    uint32_t slot;
    uint32_t generation;
    uint32_t session;
} StorageQueueItem_t;

//队列凭据和槽位当前内容分开，过期消息不能冒充新帧租借
static volatile StorageQueueItem_t s_queue[APP_STORAGE_SLOT_COUNT];

//队列头和数量只在短临界区提交
static volatile uint32_t s_head, s_count;

//命令与帧队列分开，满帧队列不能吞掉停止请求
static volatile uint32_t s_commands[STORAGE_COMMANDS], s_command_head, s_command_count;

//保存冷启动策略，不向下层传播Application全局变量
static AppStorageConfig_t s_config;

//状态是唯一决策来源，Debug只作外部观察
static StorageState_t s_state;

//达到容量或分段限制后只回收余下帧租借
static bool s_limit_reached;

//保存接收入口、收尾意图及单次抓拍是否已取得帧租借
static volatile bool s_prepared, s_accepting, s_stop_pending, s_snapshot_taken;

//会话号防止旧帧租借混入新录像，文件编号不覆盖旧文件
static volatile uint32_t s_session, s_offering, s_last_offer, s_next_file = 1U;

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

//查询接口只返回Storage任务发布的完整诊断快照
static AppStorageStatus_t s_status_snapshot;

//回读复用小型CPU缓冲，实际IDMA仍走原32KiB适配缓冲
static uint8_t s_readback[512] __attribute__((aligned(32)));

//保存正式运行结果与计数，不提供Watch写入控制入口
volatile AppStorageStatus_t g_storage_status;

//在短临界区发布完整诊断，查询不读取半更新结构
static void Storage_PublishStatus(void);

//仅普通任务环境可发保存命令，不在ISR执行文件操作
static bool Storage_Context(void);

//短临界区只投递命令，不持锁等待队列或SD
static SysResult_t Storage_Request(uint32_t command);

//事件分发与实际文件操作分离，不在状态退出时隐式释放外部资源
static void Storage_HandleEvent(uint32_t command);

//空闲状态才允许挂载，命令被受理不等于文件已创建
static void Storage_OnStart(uint32_t command);

//停止只关闭新帧租借入口，完成需要后续排空和文件关闭
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
static SysResult_t Storage_Snapshot(const StorageSlot_t *slot);

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
    if (config->partition > 4U || config->record_fps == 0U || config->record_fps > 60U || config->segment_bytes < 131072U ||
        config->segment_bytes > 0x10000000UL || config->segment_ms == 0U || config->segment_ms > 3600000U ||
        config->total_bytes < config->segment_bytes || config->total_bytes > 0x40000000UL ||
        config->reserve_bytes > 0x40000000UL || config->max_segments == 0U || config->max_segments > 1000U)
        return SYS_ERR_INVALID_ARG;
    s_config = *config;
    Platform_SyncMemoryBarrier();
    s_prepared = true;
    g_storage_status.ready = 1U;
    Storage_PublishStatus();
    return SYS_OK;
}

//异步受理录像请求
SysResult_t App_StorageStartRecording(void) { return Storage_Request(1U); }

//异步受理单张抓拍，不自动改变JPEG采集配置
SysResult_t App_StorageRequestSnapshot(void) { return Storage_Request(2U); }

//立即关闭新帧租借入口，再由Storage任务确认排空和关闭
SysResult_t App_StorageStop(void)
{
    SysResult_t result = Storage_Request(3U);
    uint32_t mask;
    if (result == SYS_OK)
    {
        mask = Platform_SyncGetPrimask();
        Platform_SyncDisableIrq();
        Platform_SyncMemoryBarrier();
        s_accepting = false;
        Platform_SyncMemoryBarrier();
        Platform_SyncRestoreIrq(mask);
    }
    return result;
}

//显式请求恢复，不隐式格式化或删除文件
SysResult_t App_StorageRecover(void) { return Storage_Request(4U); }

//只提供观察帧租借，外部不得据此归还图像
SysResult_t App_StorageGetStatus(AppStorageStatus_t *status)
{
    uint32_t mask;
    if (status == NULL || !Storage_Context())
        return SYS_ERR_INVALID_ARG;
    mask = Platform_SyncGetPrimask();
    Platform_SyncDisableIrq();
    Platform_SyncMemoryBarrier();
    *status = s_status_snapshot;
    Platform_SyncMemoryBarrier();
    Platform_SyncRestoreIrq(mask);
    return SYS_OK;
}

//存储采样使用独立目标，不依赖Windows是否开始收流
bool App_StorageWantsFrame(uint32_t now)
{
    bool wanted;
    uint32_t mask = Platform_SyncGetPrimask();
    Platform_SyncDisableIrq();
    Platform_SyncMemoryBarrier();
    wanted = s_prepared && s_accepting && !s_snapshot_taken &&
        s_slots[0].state == SLOT_FREE &&
        (!s_offer_seen || s_mode == APP_STORAGE_SNAPSHOT ||
         (uint32_t)(now - s_last_offer) >= (1000U + s_config.record_fps - 1U) / s_config.record_fps);
    Platform_SyncMemoryBarrier();
    Platform_SyncRestoreIrq(mask);
    return wanted;
}

//只在Camera持有原帧时借用，槽位满直接拒绝
SysResult_t App_StorageOfferFrame(const CameraFrame_t *frame, const AppFrameMeta_t *meta)
{
    uint32_t mask, slot, generation, session;
    CameraMjpegLease_t lease;
    SysResult_t result;

    //Camera独占Media任务侧接口，不允许Storage或ISR自行借帧
    if (!App_EventsIsOwner(RUNTIME_TARGET_CAMERA) || meta == NULL || !s_prepared)
        return SYS_ERR_INVALID_STATE;
    if (!App_StorageWantsFrame(meta->timestamp_ms))
        return SYS_ERR_BUSY;

    //这里只检查描述边界，完整JPEG扫描和CRC留给低优先级Storage
    if (frame == NULL || frame->format != CAMERA_PIXEL_FORMAT_JPEG || frame->data == NULL ||
        meta->frame_id == 0U || meta->stream_epoch == 0U || meta->width != frame->width ||
        meta->height != frame->height || frame->size_bytes > APP_STORAGE_FRAME_LIMIT ||
        frame->size_bytes < 4U || frame->size_bytes2 >= frame->size_bytes ||
        (frame->size_bytes2 != 0U && frame->data2 == NULL))
    {
        g_storage_status.dropped_frames++;
        return SYS_ERR_INVALID_ARG;
    }
    mask = Platform_SyncGetPrimask();
    Platform_SyncDisableIrq();
    Platform_SyncMemoryBarrier();

    //未确认归还的描述槽不能重用，满队列只拒绝SD
    for (slot = 0U; slot < APP_STORAGE_SLOT_COUNT; slot++)
    {
        if (s_slots[slot].state == SLOT_FREE && s_slots[slot].generation != UINT32_MAX)
            break;
    }
    if (!s_accepting || slot == APP_STORAGE_SLOT_COUNT)
    {
        g_storage_status.dropped_frames++;
        Platform_SyncRestoreIrq(mask);
        return SYS_ERR_NO_RESOURCE;
    }
    s_slots[slot].state = SLOT_RESERVING;
    generation = ++s_slots[slot].generation;
    session = s_session;
    s_offering++;
    Platform_SyncRestoreIrq(mask);

    //只增加SD所有权位，不借用图像，也不增加整帧扫描耗时
    result = CameraMjpegFifo_RetainFrame(frame, &lease);
    mask = Platform_SyncGetPrimask();
    Platform_SyncDisableIrq();
    Platform_SyncMemoryBarrier();
    s_offering--;
    if (result == SYS_OK)
    {
        s_slots[slot].frame = *frame;
        s_slots[slot].lease = lease;
        s_slots[slot].size = frame->size_bytes;
        s_slots[slot].meta = *meta;
        s_slots[slot].session = session;

        //Stop若已关闭入口，只排入归还，不让租借凭空消失
        if (s_accepting && session == s_session && s_count < APP_STORAGE_SLOT_COUNT)
        {
            s_slots[slot].state = SLOT_QUEUED;
            s_queue[(s_head + s_count) % APP_STORAGE_SLOT_COUNT].slot = slot;
            s_queue[(s_head + s_count) % APP_STORAGE_SLOT_COUNT].generation = generation;
            s_queue[(s_head + s_count) % APP_STORAGE_SLOT_COUNT].session = session;
            s_count++;
            g_storage_status.queue_depth = s_count;
            if (s_count > g_storage_status.queue_high_water)
                g_storage_status.queue_high_water = s_count;
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
            s_slots[slot].state = SLOT_RETURN_PENDING;
            g_storage_status.pending_returns++;
            result = SYS_ERR_INVALID_STATE;
        }
    }
    else
    {
        s_slots[slot].state = SLOT_FREE;
        g_storage_status.dropped_frames++;
    }
    Platform_SyncMemoryBarrier();
    Platform_SyncRestoreIrq(mask);
    (void)RuntimeEvent_Post(RUNTIME_TARGET_STORAGE, RUNTIME_EVENT_WAKE);
    return result;
}

//Camera逐项确认SD归还，失败时保留原凭据供后续重试
void App_StorageProcessReturns(void)
{
    uint32_t i, mask, generation;
    CameraMjpegLease_t lease;
    SysResult_t result;
    if (!App_EventsIsOwner(RUNTIME_TARGET_CAMERA))
        return;
    for (i = 0U; i < APP_STORAGE_SLOT_COUNT; i++)
    {
        mask = Platform_SyncGetPrimask();
        Platform_SyncDisableIrq();
        Platform_SyncMemoryBarrier();
        if (s_slots[i].state != SLOT_RETURN_PENDING)
        {
            Platform_SyncRestoreIrq(mask);
            continue;
        }
        s_slots[i].state = SLOT_RETURNING;
        generation = s_slots[i].generation;
        lease = s_slots[i].lease;
        Platform_SyncRestoreIrq(mask);

        //仅Camera访问Media，文件关闭不等于其他消费者也已归还
        result = CameraMjpegFifo_ReleaseLease(&lease);
        mask = Platform_SyncGetPrimask();
        Platform_SyncDisableIrq();
        Platform_SyncMemoryBarrier();
        g_storage_status.return_result = result;
        if (s_slots[i].generation == generation && s_slots[i].state == SLOT_RETURNING)
        {
            if (result == SYS_OK)
            {
                s_slots[i].state = SLOT_FREE;
                g_storage_status.pending_returns--;
                g_storage_status.returned_frames++;
            }
            else
                s_slots[i].state = SLOT_RETURN_PENDING;
        }
        Platform_SyncMemoryBarrier();
        Platform_SyncRestoreIrq(mask);
    }
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
    mask = Platform_SyncGetPrimask();
    Platform_SyncDisableIrq();
    Platform_SyncMemoryBarrier();
    if (s_command_count != 0U)
    {
        command = s_commands[s_command_head];
        s_command_head = (s_command_head + 1U) % STORAGE_COMMANDS;
        s_command_count--;
    }
    Platform_SyncMemoryBarrier();
    Platform_SyncRestoreIrq(mask);
    if (command != 0U)
        Storage_HandleEvent(command);
    mask = Platform_SyncGetPrimask();
    Platform_SyncDisableIrq();
    Platform_SyncMemoryBarrier();
    if (s_count != 0U)
    {
        queued = s_queue[s_head];
        s_head = (s_head + 1U) % APP_STORAGE_SLOT_COUNT;
        s_count--;
        g_storage_status.queue_depth = s_count;

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
    Platform_SyncMemoryBarrier();
    Platform_SyncRestoreIrq(mask);
    if (invalid_queue)
        Storage_Fail(SYS_ERR_PROTOCOL);
    if (slot != APP_STORAGE_SLOT_COUNT)
    {
        if (item.session != s_session || !s_mounted || s_state == STORAGE_FAULT)
            result = SYS_ERR_INVALID_STATE;
        else
        {

            //扫描与CRC在Storage任务执行，期间原帧仍持有SD引用
            result = JpegFrame_Validate(&item.frame);
            if (result == SYS_OK)
            {
                item.crc = JpegFrame_Crc32(item.frame.data, item.size - item.frame.size_bytes2, UINT32_MAX);
                if (item.frame.size_bytes2 != 0U)
                    item.crc = JpegFrame_Crc32(item.frame.data2, item.frame.size_bytes2, item.crc);
                item.crc = ~item.crc;
            }
        }
        if (result == SYS_OK && s_mode == APP_STORAGE_SNAPSHOT)
            result = Storage_Snapshot(&item);
        else if (result == SYS_OK)
        {
            amount = item.size + 24U + (item.size & 1U);
            if (amount > s_free_budget || s_free_budget - amount < s_config.reserve_bytes)
            {

                //预留空间不足时正常封尾已有完整帧，不故意写到文件系统报满
                s_stop_pending = true;
                s_limit_reached = true;
                g_storage_status.stop_reason = 3U;
            }

            //总预算耗尽时只排空并丢弃待写帧租借，不创建空的下一段
            if (result == SYS_OK && s_file == 0U && !s_limit_reached &&
                (s_segments >= s_config.max_segments ||
                 s_total_bytes + 224U + amount + 8U > s_config.total_bytes))
            {
                s_stop_pending = true;
                s_limit_reached = true;
                g_storage_status.stop_reason = 2U;
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
                    g_storage_status.stop_reason = 2U;
                }
            }
            if (s_limit_reached)
                g_storage_status.dropped_frames++;
            if (result == SYS_OK && !s_limit_reached)
            {
                if (s_avi.frames == 0U)
                    s_segment_tick = item.meta.timestamp_ms;
                limit = s_config.segment_bytes;
                if (limit > s_config.total_bytes - s_total_bytes)
                    limit = s_config.total_bytes - s_total_bytes;
                result = MjpegAvi_AppendFrame(&s_avi, &item.frame, item.meta.timestamp_ms, limit);
                if (result == SYS_OK)
                {
                    s_free_budget -= amount;
                    g_storage_status.saved_frames++;

                    //周期同步在低优先级存储任务进行，不修改AVI已接收帧的所有权
                    if ((uint32_t)(Platform_TimeGetMs() - s_sync_tick) >= 1000U)
                    {
                        result = StorageFS_Sync(s_file);
                        s_sync_tick = Platform_TimeGetMs();
                    }
                }
            }
        }
        g_storage_status.last_size = item.size;
        g_storage_status.last_crc = item.crc;
        g_storage_status.frame_id = item.meta.frame_id;
        g_storage_status.timestamp_ms = item.meta.timestamp_ms;
        g_storage_status.stream_epoch = item.meta.stream_epoch;
        (void)Storage_Release(slot, generation);
        if (result != SYS_OK)
            Storage_Fail(result);
        else if (s_mode == APP_STORAGE_SNAPSHOT)
        {
            g_storage_status.stop_reason = 5U;
            Storage_Finish(SYS_OK);

            //JPG只有验证、关闭和卸载均成功才完成本次抓拍
            if (s_state == STORAGE_IDLE)
            {
                g_storage_status.saved_files++;
                g_storage_status.saved_frames++;
            }
        }
    }
    mask = Platform_SyncGetPrimask();
    Platform_SyncDisableIrq();
    Platform_SyncMemoryBarrier();
    if (s_stop_pending)
        s_accepting = false;
    command = s_stop_pending && s_count == 0U && s_offering == 0U;
    Platform_SyncMemoryBarrier();
    Platform_SyncRestoreIrq(mask);
    if (command != 0U && s_state != STORAGE_FAULT)
        Storage_Finish(SYS_OK);
    start = Platform_TimeGetMs();

    //采集长期没有交付完整帧时结束存储等待，不影响USB和Camera恢复路径
    if (s_mounted && s_accepting && s_count == 0U && s_offering == 0U &&
        (uint32_t)(start - s_input_tick) >= 5000U)
        Storage_Fail(SYS_ERR_TIMEOUT);
    elapsed = start - s_fps_tick;
    if (elapsed >= 1000U)
    {
        amount = g_storage_status.saved_frames - s_fps_frames;
        g_storage_status.measured_fps_milli = amount <= 4294U ? amount * 1000000U / elapsed : 0U;
        s_fps_tick = start;
        s_fps_frames = g_storage_status.saved_frames;
    }
    Storage_PublishStatus();

    //通知位可以合并，队列仍有工作时自行保留后续处理机会
    if (s_count != 0U || s_command_count != 0U)
        (void)RuntimeEvent_Post(RUNTIME_TARGET_STORAGE, RUNTIME_EVENT_WAKE);
}

//只接受调度器内普通任务，不允许中断触发同步文件操作
static bool Storage_Context(void)
{
    return Platform_SyncGetException() == 0U && Platform_SyncGetPrimask() == 0U && Platform_SyncGetBasepri() == 0U;
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
        mask = Platform_SyncGetPrimask();
        Platform_SyncDisableIrq();
        Platform_SyncMemoryBarrier();
        g_storage_status.rejected_requests++;
        Platform_SyncMemoryBarrier();
        Platform_SyncRestoreIrq(mask);
        return SYS_ERR_NOT_SUPPORTED;
    }
    mask = Platform_SyncGetPrimask();
    Platform_SyncDisableIrq();
    Platform_SyncMemoryBarrier();
    if (s_command_count >= STORAGE_COMMANDS)
    {
        g_storage_status.rejected_requests++;
        Platform_SyncMemoryBarrier();
        Platform_SyncRestoreIrq(mask);
        return SYS_ERR_NO_RESOURCE;
    }
    s_commands[(s_command_head + s_command_count) % STORAGE_COMMANDS] = command;
    s_command_count++;
    g_storage_status.requested++;
    Platform_SyncMemoryBarrier();
    Platform_SyncRestoreIrq(mask);
    (void)RuntimeEvent_Post(RUNTIME_TARGET_STORAGE, RUNTIME_EVENT_WAKE);
    return SYS_OK;
}

//固定帧租借地址不与JPEG接收FIFO或索引区重叠


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
            g_storage_status.result = SYS_ERR_INVALID_ARG;
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
        g_storage_status.rejected_requests++;
        g_storage_status.result = SYS_ERR_BUSY;
        return;
    }
    result = StorageFS_Mount(true, (uint8_t)s_config.partition);
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
    mask = Platform_SyncGetPrimask();
    Platform_SyncDisableIrq();
    Platform_SyncMemoryBarrier();
    s_session++;
    s_mode = command == 1U ? APP_STORAGE_VIDEO : APP_STORAGE_SNAPSHOT;
    s_accepting = true;
    s_stop_pending = false;
    s_limit_reached = false;
    s_snapshot_taken = false;
    s_offer_seen = false;
    Storage_Transition(command == 1U ? STORAGE_VIDEO : STORAGE_SNAPSHOT);
    g_storage_status.mode = s_mode;
    g_storage_status.stop_reason = 0U;
    g_storage_status.session = s_session;
    g_storage_status.result = SYS_OK;
    g_storage_status.cleanup_result = SYS_OK;
    Platform_SyncMemoryBarrier();
    Platform_SyncRestoreIrq(mask);
}

//故障状态要求显式恢复，重复Stop不能伪装故障已经消失
static void Storage_OnStop(void)
{
    if (s_state == STORAGE_FAULT)
        return;
    s_accepting = false;
    g_storage_status.stop_reason = 1U;
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
    if (s_offering != 0U || s_count != 0U || s_state != STORAGE_FAULT)
    {
        g_storage_status.result = SYS_ERR_BUSY;
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
    g_storage_status.result = result;
    if (result == SYS_OK)
    {
        s_mounted = false;
        s_file = 0U;
        mask = Platform_SyncGetPrimask();
        Platform_SyncDisableIrq();
        Platform_SyncMemoryBarrier();

        //恢复文件系统不代表帧已归还，仍交给Camera确认
        for (i = 0U; i < APP_STORAGE_SLOT_COUNT; i++)
        {
            if (s_slots[i].state == SLOT_QUEUED)
            {
                s_slots[i].state = SLOT_RETURN_PENDING;
                g_storage_status.pending_returns++;
            }
        }
        Platform_SyncMemoryBarrier();
        Platform_SyncRestoreIrq(mask);
        g_storage_status.cleanup_result = SYS_OK;
        Storage_Transition(STORAGE_IDLE);
    }
    (void)StorageFS_GetDiagnostics((StorageFsDiagnostics_t *)&g_storage_status.filesystem);
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
        g_storage_status.file_number = s_next_file - 1U;
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
static SysResult_t Storage_Snapshot(const StorageSlot_t *slot)
{
    char name[17];
    uint32_t value, i, n, size, done = 0U, crc = UINT32_MAX;
    SysResult_t result = Storage_OpenNew(false);
    if (result == SYS_OK)
        result = Storage_Write(slot->frame.data, slot->size - slot->frame.size_bytes2);
    if (result == SYS_OK && slot->frame.size_bytes2 != 0U)
        result = Storage_Write(slot->frame.data2, slot->frame.size_bytes2);
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
    value = g_storage_status.file_number;
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

            //读回块可能横跨原JPEG两段，分段比较而不拼接整帧
            uint32_t first = slot->size - slot->frame.size_bytes2;
            uint32_t part = done < first ? first - done : 0U;
            if (part > n)
                part = n;
            if (part != 0U && memcmp(s_readback, slot->frame.data + done, part) != 0)
                result = SYS_ERR_PROTOCOL;
            if (n > part && memcmp(s_readback + part,
                slot->frame.data2 + (done + part - first), n - part) != 0)
                result = SYS_ERR_PROTOCOL;
            crc = JpegFrame_Crc32(s_readback, n, crc);
            done += n;
        }
    }
    g_storage_status.read_crc = ~crc;
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
        g_storage_status.saved_files++;
    }
    return result;
}

//正常结束和错误清理分别记录，失败不影响Camera或USB
static void Storage_Finish(SysResult_t result)
{
    SysResult_t cleanup = SYS_OK, step;
    uint32_t mask = Platform_SyncGetPrimask();
    Platform_SyncDisableIrq();
    Platform_SyncMemoryBarrier();
    s_accepting = false;
    Platform_SyncMemoryBarrier();
    Platform_SyncRestoreIrq(mask);
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
    (void)StorageFS_GetDiagnostics((StorageFsDiagnostics_t *)&g_storage_status.filesystem);
    s_stop_pending = false;
    g_storage_status.result = result;
    g_storage_status.cleanup_result = cleanup;
    if (cleanup != SYS_OK && result == SYS_OK)
        g_storage_status.failures++;
    Storage_Transition(result == SYS_OK && cleanup == SYS_OK ? STORAGE_IDLE : STORAGE_FAULT);
    g_storage_status.mode = APP_STORAGE_OFF;
}

//拒绝新帧租借，队列里旧帧租借会在后续轮次逐个安全回收
static void Storage_Fail(SysResult_t result)
{
    uint32_t mask = Platform_SyncGetPrimask();
    uint32_t slot;
    StorageQueueItem_t queued;
    Platform_SyncDisableIrq();
    Platform_SyncMemoryBarrier();
    s_accepting = false;

    //未写入帧只转入归还队列，由Camera释放对应引用
    while (s_count != 0U)
    {
        queued = s_queue[s_head];
        slot = queued.slot;
        s_head = (s_head + 1U) % APP_STORAGE_SLOT_COUNT;
        s_count--;
        if (slot < APP_STORAGE_SLOT_COUNT && s_slots[slot].state == SLOT_QUEUED &&
            s_slots[slot].generation == queued.generation && s_slots[slot].session == queued.session)
        {
            s_slots[slot].state = SLOT_RETURN_PENDING;
            g_storage_status.pending_returns++;
        }
        g_storage_status.dropped_frames++;
    }
    g_storage_status.queue_depth = 0U;
    Platform_SyncMemoryBarrier();
    Platform_SyncRestoreIrq(mask);
    g_storage_status.failures++;
    g_storage_status.stop_reason = result == SYS_ERR_TIMEOUT ? 6U : 4U;
    Storage_Finish(result);
    (void)RuntimeEvent_Post(RUNTIME_TARGET_CAMERA, RUNTIME_EVENT_WAKE);
}

//槽位代号必须与本次WRITING一致，旧归还不能释放新帧租借
static bool Storage_Release(uint32_t slot, uint32_t generation)
{
    uint32_t mask = Platform_SyncGetPrimask();
    bool released = false;
    Platform_SyncDisableIrq();
    Platform_SyncMemoryBarrier();
    if (slot < APP_STORAGE_SLOT_COUNT && s_slots[slot].state == SLOT_WRITING &&
        s_slots[slot].generation == generation)
    {

        //写入已返回；IDMA只持有既有AXI中转区，原帧可以请求归还
        s_slots[slot].state = SLOT_RETURN_PENDING;
        g_storage_status.pending_returns++;
        released = true;
    }
    Platform_SyncMemoryBarrier();
    Platform_SyncRestoreIrq(mask);
    if (released)
        (void)RuntimeEvent_Post(RUNTIME_TARGET_CAMERA, RUNTIME_EVENT_WAKE);
    return released;
}

//状态提交集中在这里，Debug帧租借不作为内部决策来源
static void Storage_Transition(StorageState_t state)
{
    s_state = state;
    g_storage_status.state = state;
}

//完整快照只在这里提交，Camera统计写入也不能打断这次借用
static void Storage_PublishStatus(void)
{
    uint32_t mask = Platform_SyncGetPrimask();
    Platform_SyncDisableIrq();
    Platform_SyncMemoryBarrier();
    s_status_snapshot = g_storage_status;
    Platform_SyncMemoryBarrier();
    Platform_SyncRestoreIrq(mask);
}
