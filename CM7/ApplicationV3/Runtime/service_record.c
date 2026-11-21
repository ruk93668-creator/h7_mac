#include "service_record.h"
#include "../Core/app_services.h"
#include "platform_sync.h"

#define RECORD_HOLDERS 4U

typedef enum
{
    RECORD_WAITING,
    RECORD_READY,
    RECORD_FAILED,
    RECORD_RECLAIMING
} RecordState_t;

typedef struct
{
    AppV3Queue_t signal;
    const AppV3ServiceInstance_t *instance;
    TaskHandle_t holders[RECORD_HOLDERS];
    uint32_t tickets[RECORD_HOLDERS];
    uint32_t last_ticket;
    uint32_t waiters;
    SysResult_t result;
    RecordState_t state;
} RecordSlot_t;

static RecordSlot_t s_records[RUNTIME_TARGET_COUNT];
static bool s_initialized;

static uint32_t Record_Lock(void)
{
    uint32_t saved = Platform_SyncGetPrimask();
    Platform_SyncDisableIrq();
    Platform_SyncMemoryBarrier();
    return saved;
}

static void Record_Unlock(uint32_t saved)
{
    Platform_SyncMemoryBarrier();
    Platform_SyncRestoreIrq(saved);
}

static bool Record_TaskContext(void)
{
    return Platform_SyncGetException() == 0U && Platform_SyncGetPrimask() == 0U &&
        Platform_SyncGetBasepri() == 0U && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
}

static RecordSlot_t *Record_Find(RuntimeEventTarget_t owner)
{
    return s_initialized && AppV3_FindService(owner) != NULL ? &s_records[owner] : NULL;
}

SysResult_t AppV3Record_Init(void)
{
    uint32_t owner;

    //从同一服务清单派生记录，调度开始前完成队列分配。
    if (s_initialized || Platform_SyncGetException() != 0U ||
        xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
        return SYS_ERR_INVALID_STATE;

    for (owner = 0U; owner < RUNTIME_TARGET_COUNT; ++owner)
    {
        if (AppV3_FindService((RuntimeEventTarget_t)owner) == NULL)
            continue;
        SysResult_t result = AppV3Queue_Alloc(sizeof(uint32_t), 1U, &s_records[owner].signal);
        if (result != SYS_OK)
            return result;
    }

    s_initialized = true;
    return SYS_OK;
}

static SysResult_t Record_Publish(RuntimeEventTarget_t owner,
    const AppV3ServiceInstance_t *instance, SysResult_t result)
{
    RecordSlot_t *slot = Record_Find(owner);
    uint32_t saved, token = 1U;

    if (slot == NULL)
        return SYS_ERR_INVALID_ARG;
    if (!Record_TaskContext() || !App_MessagesIsOwner(owner))
        return SYS_ERR_INVALID_STATE;
    saved = Record_Lock();
    if (slot->state != RECORD_WAITING)
    {
        Record_Unlock(saved);
        return SYS_ERR_ALREADY_EXISTS;
    }

    //先发布对象和结果，再唤醒等待者。非破坏性 Peek 让多位等待者看到同一结果。
    slot->instance = instance;
    slot->result = result;
    slot->state = result == SYS_OK ? RECORD_READY : RECORD_FAILED;
    Record_Unlock(saved);
    return AppV3Queue_Put(slot->signal, &token, 0U);
}

SysResult_t AppV3Record_Create(RuntimeEventTarget_t owner, const AppV3ServiceInstance_t *instance)
{
    if (instance == NULL || instance->owner != owner || instance->config == NULL)
        return SYS_ERR_INVALID_ARG;
    return Record_Publish(owner, instance, SYS_OK);
}

SysResult_t AppV3Record_Fail(RuntimeEventTarget_t owner, SysResult_t result)
{
    if (result >= SYS_OK)
        return SYS_ERR_INVALID_ARG;
    return Record_Publish(owner, NULL, result);
}

static SysResult_t Record_Acquire(RecordSlot_t *slot, RuntimeEventTarget_t owner,
    TaskHandle_t task, AppV3RecordRef_t *out)
{
    uint32_t index;

    //只在持有保护区时分配票据，代次耗尽后拒绝打开。
    if (slot->state == RECORD_FAILED)
        return slot->result;
    if (slot->state != RECORD_READY)
        return SYS_ERR_BUSY;
    if (slot->last_ticket == UINT32_MAX)
        return SYS_ERR_NO_RESOURCE;

    for (index = 0U; index < RECORD_HOLDERS; ++index)
    {
        if (slot->holders[index] != NULL)
            continue;
        slot->holders[index] = task;
        slot->tickets[index] = ++slot->last_ticket;
        *out = (AppV3RecordRef_t){slot->instance, slot->last_ticket, owner};
        return SYS_OK;
    }
    return SYS_ERR_NO_RESOURCE;
}

SysResult_t AppV3Record_Open(RuntimeEventTarget_t owner, TickType_t timeout, AppV3RecordRef_t *out)
{
    RecordSlot_t *slot = Record_Find(owner);
    uint32_t saved, token;
    SysResult_t result;
    TaskHandle_t task;

    if (slot == NULL || out == NULL || out->ticket != 0U || timeout == portMAX_DELAY)
        return SYS_ERR_INVALID_ARG;
    if (!Record_TaskContext())
        return SYS_ERR_INVALID_STATE;

    task = xTaskGetCurrentTaskHandle();
    saved = Record_Lock();
    if (slot->state != RECORD_WAITING || timeout == 0U)
    {
        result = Record_Acquire(slot, owner, task, out);
        Record_Unlock(saved);
        return result;
    }

    //等待者挂在原记录上，发布后也不能在等待者完成 Open 前销毁记录。
    if (slot->waiters == UINT32_MAX)
    {
        Record_Unlock(saved);
        return SYS_ERR_NO_RESOURCE;
    }
    ++slot->waiters;
    Record_Unlock(saved);
    result = AppV3Queue_Peek(slot->signal, &token, timeout);

    saved = Record_Lock();
    --slot->waiters;
    if (result == SYS_OK)
        result = Record_Acquire(slot, owner, task, out);
    Record_Unlock(saved);
    return result;
}

SysResult_t AppV3Record_Close(AppV3RecordRef_t *reference)
{
    RecordSlot_t *slot;
    uint32_t saved, index;
    TaskHandle_t task;

    if (reference == NULL || reference->ticket == 0U)
        return SYS_ERR_INVALID_ARG;
    slot = Record_Find(reference->owner);
    if (slot == NULL || !Record_TaskContext())
        return SYS_ERR_INVALID_STATE;

    task = xTaskGetCurrentTaskHandle();
    saved = Record_Lock();
    for (index = 0U; index < RECORD_HOLDERS; ++index)
    {
        if (slot->holders[index] != task || slot->tickets[index] != reference->ticket ||
            slot->instance != reference->instance)
            continue;
        slot->holders[index] = NULL;
        *reference = (AppV3RecordRef_t){0};
        Record_Unlock(saved);
        return SYS_OK;
    }
    Record_Unlock(saved);
    return SYS_ERR_NOT_FOUND;
}

SysResult_t AppV3Record_Destroy(RuntimeEventTarget_t owner)
{
    RecordSlot_t *slot = Record_Find(owner);
    uint32_t saved, index, token;
    RecordState_t old_state;
    SysResult_t result;

    if (slot == NULL || !Record_TaskContext() || !App_MessagesIsOwner(owner))
        return SYS_ERR_INVALID_STATE;

    saved = Record_Lock();
    old_state = slot->state;
    if (old_state != RECORD_READY && old_state != RECORD_FAILED)
    {
        Record_Unlock(saved);
        return SYS_ERR_INVALID_STATE;
    }
    if (slot->waiters != 0U)
    {
        Record_Unlock(saved);
        return SYS_ERR_BUSY;
    }
    for (index = 0U; index < RECORD_HOLDERS; ++index)
    {
        if (slot->holders[index] == NULL)
            continue;
        Record_Unlock(saved);
        return SYS_ERR_BUSY;
    }

    //离开临界区取走就绪标识时，暂时拒绝新的 Open。
    slot->state = RECORD_RECLAIMING;
    Record_Unlock(saved);
    result = AppV3Queue_Get(slot->signal, &token, 0U);
    saved = Record_Lock();
    if (result == SYS_OK)
    {
        slot->instance = NULL;
        slot->state = RECORD_WAITING;
    }
    else
        slot->state = old_state;
    Record_Unlock(saved);
    return result;
}

SysResult_t AppV3Record_Query(RuntimeEventTarget_t owner)
{
    RecordSlot_t *slot = Record_Find(owner);
    uint32_t saved;
    SysResult_t result;

    if (slot == NULL || !Record_TaskContext())
        return SYS_ERR_INVALID_STATE;
    saved = Record_Lock();
    result = slot->state == RECORD_READY || slot->state == RECORD_FAILED ? slot->result : SYS_ERR_BUSY;
    Record_Unlock(saved);
    return result;
}
