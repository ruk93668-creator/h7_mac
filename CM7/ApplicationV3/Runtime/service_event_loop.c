#include "service_event_loop.h"
#include "app_events.h"
#include "platform_sync.h"

#define EVENT_QUEUE_RECHECK_MS 100U

//校验线程、调度和中断状态，确保回调由所属任务执行
static bool EventLoop_IsOwner(RuntimeEventTarget_t owner)
{
    return (uint32_t)owner < RUNTIME_TARGET_COUNT && Platform_SyncGetException() == 0U &&
        Platform_SyncGetPrimask() == 0U && Platform_SyncGetBasepri() == 0U &&
        xTaskGetSchedulerState() == taskSCHEDULER_RUNNING && App_EventsIsOwner(owner);
}

//在所属线程中建立事件循环
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

//订阅队列可读提示，回调上下文由所属任务维护
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
        //池确认订阅后再提交本地回调关系
        loop->queue = queue;
        loop->context = context;
        loop->readable = readable;
    }
    return result;
}

//解除队列订阅并撤回本地回调
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

//依据真实队列判断是否需要执行回调
static SysResult_t EventLoop_Dispatch(AppV3EventLoop_t *loop, bool *readable)
{
    UBaseType_t capacity, space;
    SysResult_t result;

    *readable = false;
    if (loop->readable == NULL)
        return SYS_OK;

    //通知位只是提示，只有队列确有数据时才执行回调
    result = AppV3Queue_Query(loop->queue, &capacity, &space);
    if (result != SYS_OK || space == capacity)
        return result;

    *readable = true;
    return loop->readable(loop->queue, loop->context);
}

//执行一次队列检查、事件等待和唤醒后检查
SysResult_t AppV3EventLoop_Step(AppV3EventLoop_t *loop, uint32_t timeout_ms, uint32_t *events)
{
    SysResult_t result, dispatch_result;
    bool readable_before_wait, readable_after_wait;

    if (loop == NULL || events == NULL)
        return SYS_ERR_INVALID_ARG;
    if (!loop->initialized || !EventLoop_IsOwner(loop->owner))
        return SYS_ERR_INVALID_STATE;

    //在等待前后检查真实队列，通知合并或失败时仍保留所有入队数据。
    *events = 0U;
    result = EventLoop_Dispatch(loop, &readable_before_wait);
    if (result != SYS_OK)
        return result;
    //有订阅时限制等待，队列已有消息但通知失败仍能推进
    if (loop->readable != NULL && timeout_ms > EVENT_QUEUE_RECHECK_MS)
        timeout_ms = EVENT_QUEUE_RECHECK_MS;
    result = App_EventsWait(loop->owner, timeout_ms, events);
    if (result != SYS_OK && result != SYS_ERR_TIMEOUT)
        return result;
    //重新查询等待期间到达的消息，即使本轮等待超时
    dispatch_result = EventLoop_Dispatch(loop, &readable_after_wait);
    if (dispatch_result != SYS_OK)
        return dispatch_result;
    return readable_before_wait || readable_after_wait ? SYS_OK : result;
}
