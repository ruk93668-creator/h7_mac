#include "service_event_loop.h"
#include "app_events.h"
#include "platform_sync.h"

#define EVENT_QUEUE_RECHECK_MS 100U

static bool EventLoop_IsOwner(RuntimeEventTarget_t owner)
{
    return (uint32_t)owner < RUNTIME_TARGET_COUNT && Platform_SyncGetException() == 0U &&
        Platform_SyncGetPrimask() == 0U && Platform_SyncGetBasepri() == 0U &&
        xTaskGetSchedulerState() == taskSCHEDULER_RUNNING && App_EventsIsOwner(owner);
}

SysResult_t AppV3EventLoop_Init(AppV3EventLoop_t *loop, RuntimeEventTarget_t owner)
{
    if (loop == NULL)
        return SYS_ERR_INVALID_ARG;
    if (loop->initialized || !EventLoop_IsOwner(owner))
        return SYS_ERR_INVALID_STATE;

    //每个任务持有自己的事件循环，订阅只在该任务内变化。
    *loop = (AppV3EventLoop_t){.owner = owner, .initialized = true};
    return SYS_OK;
}

SysResult_t AppV3EventLoop_Subscribe(AppV3EventLoop_t *loop, AppV3Queue_t queue,
    AppV3QueueReadable_t readable, void *context)
{
    SysResult_t result;

    if (loop == NULL || readable == NULL)
        return SYS_ERR_INVALID_ARG;
    if (!loop->initialized || !EventLoop_IsOwner(loop->owner) || loop->readable != NULL)
        return SYS_ERR_INVALID_STATE;

    //池保留订阅关系，使队列在事件循环持有期间不能销毁。
    result = AppV3Queue_Subscribe(queue, loop->owner);
    if (result == SYS_OK)
    {
        loop->queue = queue;
        loop->context = context;
        loop->readable = readable;
    }
    return result;
}

SysResult_t AppV3EventLoop_Unsubscribe(AppV3EventLoop_t *loop)
{
    SysResult_t result;

    if (loop == NULL)
        return SYS_ERR_INVALID_ARG;
    if (!loop->initialized || !EventLoop_IsOwner(loop->owner) || loop->readable == NULL)
        return SYS_ERR_INVALID_STATE;

    //先撤销池订阅，再清除本地回调，晚到的唤醒只是一条提示。
    result = AppV3Queue_Unsubscribe(loop->queue, loop->owner);
    if (result == SYS_OK)
    {
        loop->readable = NULL;
        loop->context = NULL;
        loop->queue = (AppV3Queue_t){0};
    }
    return result;
}

static SysResult_t EventLoop_Dispatch(AppV3EventLoop_t *loop, bool *readable)
{
    UBaseType_t capacity, space;
    SysResult_t result;

    *readable = false;
    if (loop->readable == NULL)
        return SYS_OK;

    result = AppV3Queue_Query(loop->queue, &capacity, &space);
    if (result != SYS_OK || space == capacity)
        return result;

    *readable = true;
    return loop->readable(loop->queue, loop->context);
}

SysResult_t AppV3EventLoop_Step(AppV3EventLoop_t *loop, uint32_t timeout_ms, uint32_t *events)
{
    SysResult_t result, dispatched;
    bool before, after;

    if (loop == NULL || events == NULL)
        return SYS_ERR_INVALID_ARG;
    if (!loop->initialized || !EventLoop_IsOwner(loop->owner))
        return SYS_ERR_INVALID_STATE;

    //在等待前后检查真实队列，通知合并或失败时仍保留所有入队数据。
    *events = 0U;
    result = EventLoop_Dispatch(loop, &before);
    if (result != SYS_OK)
        return result;
    if (loop->readable != NULL && timeout_ms > EVENT_QUEUE_RECHECK_MS)
        timeout_ms = EVENT_QUEUE_RECHECK_MS;
    result = App_EventsWait(loop->owner, timeout_ms, events);
    if (result != SYS_OK && result != SYS_ERR_TIMEOUT)
        return result;
    dispatched = EventLoop_Dispatch(loop, &after);
    if (dispatched != SYS_OK)
        return dispatched;
    return before || after ? SYS_OK : result;
}
