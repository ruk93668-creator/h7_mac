#include "queue_pool.h"
#include "app_messages.h"
#include "platform_sync.h"
#include "queue.h"

#define MESSAGE_SLOTS 6U
#define MESSAGE_DEPTH 4U
#define SIGNAL_SLOTS 3U
#define POOL_SIZE (MESSAGE_SLOTS + SIGNAL_SLOTS)

typedef struct
{
    StaticQueue_t control;
    QueueHandle_t queue;
    uint32_t generation;
    UBaseType_t capacity;
    uint16_t users;
    uint8_t target;
    bool reserved;
} QueueSlot_t;

static QueueSlot_t s_slots[POOL_SIZE];
static uint8_t s_messages[MESSAGE_SLOTS][MESSAGE_DEPTH * sizeof(AppMessage_t)] __attribute__((aligned(8)));
static uint32_t s_signals[SIGNAL_SLOTS];

static uint32_t QueuePool_Lock(void)
{
    uint32_t saved = Platform_SyncGetPrimask();
    //短临界区同时保护任务与合法 RTOS ISR 对池的访问。
    Platform_SyncDisableIrq();
    Platform_SyncMemoryBarrier();
    return saved;
}

static void QueuePool_Unlock(uint32_t saved)
{
    Platform_SyncMemoryBarrier();
    Platform_SyncRestoreIrq(saved);
}

static QueueSlot_t *QueuePool_Find(AppV3Queue_t queue)
{
    if (queue.index >= POOL_SIZE || queue.generation == 0U)
        return NULL;

    QueueSlot_t *slot = &s_slots[queue.index];
    return slot->queue != NULL && slot->generation == queue.generation ? slot : NULL;
}

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

static void QueuePool_Unpin(QueueSlot_t *slot)
{
    uint32_t saved = QueuePool_Lock();
    --slot->users;
    QueuePool_Unlock(saved);
}

static bool QueuePool_TaskContext(TickType_t timeout)
{
    //允许调度前以零等待建立静态对象，有界阻塞仅在正常任务上下文。
    if (Platform_SyncGetException() != 0U || Platform_SyncGetPrimask() != 0U)
        return false;

    return timeout == 0U || (timeout != portMAX_DELAY &&
        Platform_SyncGetBasepri() == 0U && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING);
}

SysResult_t AppV3Queue_Alloc(size_t item_bytes, UBaseType_t length, AppV3Queue_t *out)
{
    uint32_t saved, index;
    bool signal = item_bytes <= sizeof(s_signals[0]) && length == 1U;
    uint32_t limit = signal ? POOL_SIZE : MESSAGE_SLOTS;

    if (out == NULL || item_bytes == 0U || item_bytes > sizeof(AppMessage_t) ||
        length == 0U || length > MESSAGE_DEPTH)
        return SYS_ERR_INVALID_ARG;
    if (!QueuePool_TaskContext(0))
        return SYS_ERR_INVALID_STATE;

    saved = QueuePool_Lock();
    for (index = signal ? MESSAGE_SLOTS : 0U; index < limit; ++index)
    {
        QueueSlot_t *slot = &s_slots[index];
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

    handle = slot->queue;
    slot->queue = NULL;
    slot->reserved = true;
    QueuePool_Unlock(saved);
    vQueueDelete(handle);
    saved = QueuePool_Lock();
    slot->reserved = false;
    QueuePool_Unlock(saved);
    return SYS_OK;
}

static SysResult_t QueuePool_TaskTransfer(AppV3Queue_t queue, const void *input, void *output,
    TickType_t timeout, bool peek)
{
    uint8_t target;
    QueueSlot_t *slot;
    BaseType_t result;

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
        result = xQueueSend(slot->queue, input, timeout);
        if (result == pdPASS && target < RUNTIME_TARGET_COUNT)
            (void)RuntimeEvent_Post((RuntimeEventTarget_t)target, RUNTIME_EVENT_WAKE);
    }
    else if (peek)
        result = xQueuePeek(slot->queue, output, timeout);
    else
        result = xQueueReceive(slot->queue, output, timeout);
    QueuePool_Unpin(slot);
    return result == pdPASS ? SYS_OK : (timeout != 0U ? SYS_ERR_TIMEOUT : SYS_ERR_BUSY);
}

SysResult_t AppV3Queue_Put(AppV3Queue_t queue, const void *item, TickType_t timeout)
{
    return QueuePool_TaskTransfer(queue, item, NULL, timeout, false);
}

SysResult_t AppV3Queue_Get(AppV3Queue_t queue, void *item, TickType_t timeout)
{
    return QueuePool_TaskTransfer(queue, NULL, item, timeout, false);
}

SysResult_t AppV3Queue_Peek(AppV3Queue_t queue, void *item, TickType_t timeout)
{
    return QueuePool_TaskTransfer(queue, NULL, item, timeout, true);
}

static SysResult_t QueuePool_IsrTransfer(AppV3Queue_t queue, const void *input, void *output)
{
    uint8_t target;
    QueueSlot_t *slot;
    BaseType_t result, yield = pdFALSE;

    if (input == NULL && output == NULL)
        return SYS_ERR_INVALID_ARG;
    if (Platform_SyncGetException() == 0U)
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
    QueuePool_Unpin(slot);
    portYIELD_FROM_ISR(yield);
    return result == pdPASS ? SYS_OK : SYS_ERR_BUSY;
}

SysResult_t AppV3Queue_PutFromIsr(AppV3Queue_t queue, const void *item)
{
    return QueuePool_IsrTransfer(queue, item, NULL);
}

SysResult_t AppV3Queue_GetFromIsr(AppV3Queue_t queue, void *item)
{
    return QueuePool_IsrTransfer(queue, NULL, item);
}

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

    *capacity = slot->capacity;
    *space = uxQueueSpacesAvailable(slot->queue);
    QueuePool_Unpin(slot);
    return SYS_OK;
}

static SysResult_t QueuePool_Subscription(AppV3Queue_t queue, RuntimeEventTarget_t owner, bool subscribe)
{
    QueueSlot_t *slot;
    uint32_t saved;
    SysResult_t result = SYS_ERR_INVALID_STATE;

    if ((uint32_t)owner >= RUNTIME_TARGET_COUNT || !QueuePool_TaskContext(0) || !App_EventsIsOwner(owner))
        return SYS_ERR_INVALID_STATE;

    saved = QueuePool_Lock();
    slot = QueuePool_Find(queue);
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

SysResult_t AppV3Queue_Subscribe(AppV3Queue_t queue, RuntimeEventTarget_t owner)
{
    return QueuePool_Subscription(queue, owner, true);
}

SysResult_t AppV3Queue_Unsubscribe(AppV3Queue_t queue, RuntimeEventTarget_t owner)
{
    return QueuePool_Subscription(queue, owner, false);
}
