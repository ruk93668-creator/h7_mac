#include "queue_pool.h"
#include "app_messages.h"
#include "platform_sync.h"
#include "queue.h"

//八条物理帧/业务通道与四个服务邮箱，共十二个静态队列
#define MESSAGE_SLOTS 12U
//每个业务队列可保存的最大消息条数
#define MESSAGE_DEPTH 4U
//只读记录通知使用的静态信号队列配额
#define SIGNAL_SLOTS 4U
//静态队列表总槽数，包含业务与记录通知
#define POOL_SIZE (MESSAGE_SLOTS + SIGNAL_SLOTS)

//队列池槽位状态，由池临界区保护
typedef struct
{
    //队列的内核控制块，只有确认销毁后才可复用
    StaticQueue_t control;
    //绑定当前对象的内核或应用队列句柄
    QueueHandle_t queue;
    //槽位或采集实例的代号，用于排除已销毁凭据
    uint32_t generation;
    //预留的数据容量，单位或元素数由所属对象决定
    UBaseType_t capacity;
    //已经固定该槽的访问数，归零后才允许回收
    uint16_t users;
    //接收队列进展通知的任务编号
    uint8_t target;
    //槽已被分配或保留的标志
    bool reserved;
} QueueSlot_t;

//槽位独占静态控制块，句柄代次和订阅关系都由池维护
static QueueSlot_t s_slots[POOL_SIZE];
//宽槽保存原应用消息，短信号使用独立空间
static uint8_t s_messages[MESSAGE_SLOTS][MESSAGE_DEPTH * sizeof(AppMessage_t)] __attribute__((aligned(8)));
//固定记录通知存储，每条队列拥有自己的槽
static uint32_t s_signals[SIGNAL_SLOTS];

//保存并关闭全局中断，保护任务和ISR共享的槽位
static uint32_t QueuePool_Lock(void)
{
    uint32_t saved = Platform_SyncGetPrimask();
    //短临界区同时保护任务与合法 RTOS ISR 对池的访问。
    Platform_SyncDisableIrq();
    Platform_SyncMemoryBarrier();
    return saved;
}

//先完成槽位状态写入，再恢复调用前的中断状态
static void QueuePool_Unlock(uint32_t saved)
{
    Platform_SyncMemoryBarrier();
    Platform_SyncRestoreIrq(saved);
}

//用槽位和代次查找现存队列，拒绝已经回收的句柄
static QueueSlot_t *QueuePool_Find(AppV3Queue_t queue)
{
    if (queue.index >= POOL_SIZE || queue.generation == 0U)
        return NULL;

    QueueSlot_t *slot = &s_slots[queue.index];
    return slot->queue != NULL && slot->generation == queue.generation ? slot : NULL;
}

//持有槽位后再离开临界区，避免收发过程中被销毁
static QueueSlot_t *QueuePool_Pin(AppV3Queue_t queue, uint8_t *target)
{
    uint32_t saved = QueuePool_Lock();
    QueueSlot_t *slot = QueuePool_Find(queue);
    if (slot != NULL && slot->users != UINT16_MAX)
    {
        ++slot->users;
        *target = slot->target;
    }
    else
        slot = NULL;

    QueuePool_Unlock(saved);
    return slot;
}

//收发结束后解除销毁门禁
static void QueuePool_Unpin(QueueSlot_t *slot)
{
    uint32_t saved = QueuePool_Lock();
    --slot->users;
    QueuePool_Unlock(saved);
}

//检查任务上下文和等待参数
static bool QueuePool_TaskContext(TickType_t timeout)
{
    //允许调度前以零等待建立静态对象，有界阻塞仅在正常任务上下文。
    if (Platform_SyncGetException() != 0U || Platform_SyncGetPrimask() != 0U)
        return false;

    return timeout == 0U || (timeout != portMAX_DELAY &&
        Platform_SyncGetBasepri() == 0U && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING);
}

//按消息尺寸从固定容量等级取得静态队列
SysResult_t AppV3Queue_Alloc(size_t item_bytes, UBaseType_t length, AppV3Queue_t *out)
{
    uint32_t saved, index;
    bool signal = item_bytes <= sizeof(s_signals[0]) && length == 1U;
    uint32_t limit = signal ? POOL_SIZE : MESSAGE_SLOTS;

    //先检查消息尺寸和容量，避免超出静态负载区
    if (out == NULL || item_bytes == 0U || item_bytes > sizeof(AppMessage_t) ||
        length == 0U || length > MESSAGE_DEPTH)
        return SYS_ERR_INVALID_ARG;
    if (!QueuePool_TaskContext(0))
        return SYS_ERR_INVALID_STATE;

    saved = QueuePool_Lock();
    for (index = signal ? MESSAGE_SLOTS : 0U; index < limit; ++index)
    {
        QueueSlot_t *slot = &s_slots[index];
        //跳过已分配、正在销毁或代次耗尽的槽
        if (slot->queue != NULL || slot->reserved || slot->generation == UINT32_MAX)
            continue;

        //池元数据与静态控制块由这个槽唯一拥有，旧代次不能命中新队列。
        uint8_t *buffer = signal ? (uint8_t *)&s_signals[index - MESSAGE_SLOTS] : s_messages[index];
        slot->queue = xQueueCreateStatic(length, (UBaseType_t)item_bytes, buffer, &slot->control);
        if (slot->queue == NULL)
            break;
        slot->capacity = length;
        slot->target = RUNTIME_TARGET_COUNT;
        out->index = (uint8_t)index;
        out->generation = ++slot->generation;
        QueuePool_Unlock(saved);
        return SYS_OK;
    }

    QueuePool_Unlock(saved);
    return SYS_ERR_NO_RESOURCE;
}

//仅回收没有订阅、在途操作和待消费数据的队列
SysResult_t AppV3Queue_Free(AppV3Queue_t queue)
{
    QueueSlot_t *slot;
    QueueHandle_t handle;
    uint32_t saved;

    if (!QueuePool_TaskContext(0))
        return SYS_ERR_INVALID_STATE;

    saved = QueuePool_Lock();
    slot = QueuePool_Find(queue);
    if (slot == NULL)
    {
        QueuePool_Unlock(saved);
        return SYS_ERR_NOT_FOUND;
    }

    //等待中的收发、订阅和未消费数据必须先退场。
    if (slot->users != 0U || slot->target != RUNTIME_TARGET_COUNT ||
        uxQueueMessagesWaiting(slot->queue) != 0U)
    {
        QueuePool_Unlock(saved);
        return SYS_ERR_BUSY;
    }

    //先使旧句柄不可查找，底层销毁完成前仍占用该槽
    handle = slot->queue;
    slot->queue = NULL;
    slot->reserved = true;
    QueuePool_Unlock(saved);
    //底层销毁在池临界区之外执行
    vQueueDelete(handle);
    saved = QueuePool_Lock();
    slot->reserved = false;
    QueuePool_Unlock(saved);
    return SYS_OK;
}

//执行任务上下文的入队、出队或非破坏性读取
static SysResult_t QueuePool_TaskTransfer(AppV3Queue_t queue, const void *input, void *output,
    TickType_t timeout, bool peek)
{
    uint8_t target;
    QueueSlot_t *slot;
    BaseType_t result;

    //收发操作必须提供消息存储
    if (input == NULL && output == NULL)
        return SYS_ERR_INVALID_ARG;
    if (!QueuePool_TaskContext(timeout))
        return SYS_ERR_INVALID_STATE;
    slot = QueuePool_Pin(queue, &target);
    if (slot == NULL)
        return SYS_ERR_NOT_FOUND;

    //内核负责实际复制与唤醒阻塞者，在此期间保持销毁门禁。
    if (input != NULL)
    {
        //入队成功即已保存消息，通知失败只影响唤醒时机
        result = xQueueSend(slot->queue, input, timeout);
        if (result == pdPASS && target < RUNTIME_TARGET_COUNT)
            (void)RuntimeEvent_Post((RuntimeEventTarget_t)target, RUNTIME_EVENT_WAKE);
    }
    //区分保留队首的读取与实际消费
    else if (peek)
        result = xQueuePeek(slot->queue, output, timeout);
    else
        result = xQueueReceive(slot->queue, output, timeout);
    //底层操作和通知返回后允许其他任务重新检查是否可销毁
    QueuePool_Unpin(slot);
    return result == pdPASS ? SYS_OK : (timeout != 0U ? SYS_ERR_TIMEOUT : SYS_ERR_BUSY);
}

//提交消息，等待上限由调用者指定
SysResult_t AppV3Queue_Put(AppV3Queue_t queue, const void *item, TickType_t timeout)
{
    return QueuePool_TaskTransfer(queue, item, NULL, timeout, false);
}

//取走队首消息，等待上限由调用者指定
SysResult_t AppV3Queue_Get(AppV3Queue_t queue, void *item, TickType_t timeout)
{
    return QueuePool_TaskTransfer(queue, NULL, item, timeout, false);
}

//读取队首消息并保留内容，供Record等待者共享就绪结果
SysResult_t AppV3Queue_Peek(AppV3Queue_t queue, void *item, TickType_t timeout)
{
    return QueuePool_TaskTransfer(queue, NULL, item, timeout, true);
}

//在允许调用FreeRTOS的IRQ中执行零等待收发
static SysResult_t QueuePool_IsrTransfer(AppV3Queue_t queue, const void *input, void *output)
{
    uint8_t target;
    QueueSlot_t *slot;
    BaseType_t result, yield = pdFALSE;
    uint32_t exception = Platform_SyncGetException();

    //收发操作必须提供消息存储
    if (input == NULL && output == NULL)
        return SYS_ERR_INVALID_ARG;
    //与 App_Events 一致，仅允许可调用 FreeRTOS 的外设 IRQ。
    if (exception < 16U || Platform_SyncGetIrqPriority(exception - 16U) <
        configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY)
        return SYS_ERR_INVALID_STATE;
    portASSERT_IF_INTERRUPT_PRIORITY_INVALID();
    slot = QueuePool_Pin(queue, &target);
    if (slot == NULL)
        return SYS_ERR_NOT_FOUND;

    if (input != NULL)
    {
        result = xQueueSendFromISR(slot->queue, input, &yield);
        if (result == pdPASS && target < RUNTIME_TARGET_COUNT)
            (void)RuntimeEvent_PostFromIsr((RuntimeEventTarget_t)target, RUNTIME_EVENT_WAKE);
    }
    else
        result = xQueueReceiveFromISR(slot->queue, output, &yield);
    //底层操作和通知返回后允许其他任务重新检查是否可销毁
    QueuePool_Unpin(slot);
    //只在内核确认唤醒高优先级任务时请求调度
    portYIELD_FROM_ISR(yield);
    return result == pdPASS ? SYS_OK : SYS_ERR_BUSY;
}

//从ISR提交消息，队列满时保留已入队内容
SysResult_t AppV3Queue_PutFromIsr(AppV3Queue_t queue, const void *item)
{
    return QueuePool_IsrTransfer(queue, item, NULL);
}

//从ISR取走一条已入队消息
SysResult_t AppV3Queue_GetFromIsr(AppV3Queue_t queue, void *item)
{
    return QueuePool_IsrTransfer(queue, NULL, item);
}

//查询队列容量和剩余槽位，查询期间保持槽位有效
SysResult_t AppV3Queue_Query(AppV3Queue_t queue, UBaseType_t *capacity, UBaseType_t *space)
{
    uint8_t target;
    QueueSlot_t *slot;

    if (capacity == NULL || space == NULL || capacity == space)
        return SYS_ERR_INVALID_ARG;
    if (!QueuePool_TaskContext(0))
        return SYS_ERR_INVALID_STATE;

    slot = QueuePool_Pin(queue, &target);
    if (slot == NULL)
        return SYS_ERR_NOT_FOUND;

    //容量固定，剩余槽位取自内核队列的当前状态
    *capacity = slot->capacity;
    *space = uxQueueSpacesAvailable(slot->queue);
    //底层操作和通知返回后允许其他任务重新检查是否可销毁
    QueuePool_Unpin(slot);
    return SYS_OK;
}

//由目标任务建立或撤销唯一的队列唤醒关系
static SysResult_t QueuePool_Subscription(AppV3Queue_t queue, RuntimeEventTarget_t owner, bool subscribe)
{
    QueueSlot_t *slot;
    uint32_t saved;
    SysResult_t result = SYS_ERR_INVALID_STATE;

    if ((uint32_t)owner >= RUNTIME_TARGET_COUNT || !QueuePool_TaskContext(0) || !App_EventsIsOwner(owner))
        return SYS_ERR_INVALID_STATE;

    saved = QueuePool_Lock();
    slot = QueuePool_Find(queue);
    //过期句柄不能修改复用后的订阅关系
    if (slot == NULL)
        result = SYS_ERR_NOT_FOUND;
    else if (subscribe && slot->target == RUNTIME_TARGET_COUNT)
    {
        slot->target = (uint8_t)owner;
        result = SYS_OK;
    }
    else if (!subscribe && slot->target == owner)
    {
        slot->target = RUNTIME_TARGET_COUNT;
        result = SYS_OK;
    }
    QueuePool_Unlock(saved);
    return result;
}

//绑定队列可读提示的接收任务
SysResult_t AppV3Queue_Subscribe(AppV3Queue_t queue, RuntimeEventTarget_t owner)
{
    return QueuePool_Subscription(queue, owner, true);
}

//撤销所属任务的队列可读提示
SysResult_t AppV3Queue_Unsubscribe(AppV3Queue_t queue, RuntimeEventTarget_t owner)
{
    return QueuePool_Subscription(queue, owner, false);
}
