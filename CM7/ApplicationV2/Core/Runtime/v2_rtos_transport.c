// 九条单生产者/单消费者的 CM7 内部队列。通知只提示，队列复制才提交。
#include "v2_rtos_transport.h"
#include "app_events.h"
#include "platform_sync.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

typedef struct
{
    StaticQueue_t control;
    QueueHandle_t handle;
} V2RtosQueue;

typedef struct
{
    RuntimeEventTarget_t task;
    V2RtosQueue request;
    V2RtosQueue completion;
    V2RtosQueue consumed;
} V2RtosRoute;

static V2RtosRoute s_camera = {.task = RUNTIME_TARGET_CAMERA};
static V2RtosRoute s_usb = {.task = RUNTIME_TARGET_USB};
static V2RtosRoute s_storage = {.task = RUNTIME_TARGET_STORAGE};
// 创建结果在调度器启动前写入。运行时每个任务独占自己的 BOOT 记录。
static bool s_attempted, s_ready;
static bool s_boot[RUNTIME_TARGET_COUNT];

#define BUFFER_SET(name, Reply)                                                                    \
    static LCRequest name##_requests[V2_RTOS_DEPTH];                                               \
    static Reply name##_replies[V2_RTOS_DEPTH];                                                    \
    static LCCompletion name##_acks[V2_RTOS_DEPTH]

BUFFER_SET(s_camera, VCameraReply);
BUFFER_SET(s_usb, VUSBReply);
BUFFER_SET(s_storage, VStorageReply);

// 绑定静态控制块及存储区，避免在任务运行期间分配。
static void create(V2RtosQueue *queue, size_t bytes, void *buffer)
{
    queue->handle = xQueueCreateStatic(V2_RTOS_DEPTH, bytes, buffer, &queue->control);
}

#define INIT_ROUTE(name)                                                                           \
    create(&name.request, sizeof(name##_requests[0]), name##_requests);                            \
    create(&name.completion, sizeof(name##_replies[0]), name##_replies);                           \
    create(&name.consumed, sizeof(name##_acks[0]), name##_acks)

// 仅当本路三条队列都建立成功时才可使用。
static bool ready(const V2RtosRoute *route)
{
    return route->request.handle && route->completion.handle && route->consumed.handle;
}

// 调度器启动前创建九条队列，在全部成功后开放。
SysResult_t V2Rtos_Init(void)
{
    if (s_attempted || Platform_SyncGetException() || Platform_SyncGetPrimask() ||
        xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
        return SYS_ERR_INVALID_STATE;
    // 静态队列创建失败时保持入口关闭，运行期不重新创建。
    s_attempted = true;
    INIT_ROUTE(s_camera);
    INIT_ROUTE(s_usb);
    INIT_ROUTE(s_storage);
    //三组运输端口都创建成功后才开放收发。
    s_ready = ready(&s_camera) && ready(&s_usb) && ready(&s_storage);
    return s_ready ? SYS_OK : SYS_ERR_NO_RESOURCE;
}

// 确认任务身份和中断屏蔽状态，避免从错误上下文访问队列。
static bool owner_context(RuntimeEventTarget_t task)
{
    return s_ready && (uint32_t)task < RUNTIME_TARGET_COUNT &&
           !Platform_SyncGetPrimask() && !Platform_SyncGetBasepri() && App_EventsIsOwner(task);
}

// 收到原 BOOT 信号后标记本任务允许推进。
bool V2Rtos_Enter(RuntimeEventTarget_t task, uint32_t events)
{
    if (!owner_context(task))
        return false;
    if (events & RUNTIME_EVENT_BOOT_READY)
        //只有本任务消费 BOOT 提示后才开放它的运输入口。
        s_boot[task] = true;
    return s_boot[task];
}

// 要求合法任务上下文以及该任务已收到 BOOT。
bool V2Rtos_Owner(RuntimeEventTarget_t task)
{
    //运行期同时要求任务身份匹配和 BOOT 完成。
    return owner_context(task) && s_boot[task];
}

// 执行非阻塞复制。队列满时发送方保留当前消息重试。
static SysResult_t send(V2RtosQueue *queue, RuntimeEventTarget_t owner, const void *message)
{
    if (!message)
        return SYS_ERR_INVALID_ARG;
    if (!V2Rtos_Owner(owner))
        return SYS_ERR_INVALID_STATE;
    return xQueueSend(queue->handle, message, 0U) == pdPASS ? SYS_OK : SYS_ERR_NO_RESOURCE;
}

// 只取一条队列消息，空队列保持输出缓冲区。
static SysResult_t receive(V2RtosQueue *queue, RuntimeEventTarget_t owner, void *message)
{
    if (!message)
        return SYS_ERR_INVALID_ARG;
    if (!V2Rtos_Owner(owner))
        return SYS_ERR_INVALID_STATE;
    return xQueueReceive(queue->handle, message, 0U) == pdPASS ? SYS_OK : SYS_ERR_NOT_FOUND;
}

// 校验当前路由对应的模块任务。
static int module_owner(void *port_context)
{
    const V2RtosRoute *route = port_context;
    return V2Rtos_Owner(route->task);
}

// 校验当前任务是否是控制与执行的唯一所有者。
static int control_owner(void *port_context)
{
    (void)port_context;
    return V2Rtos_Owner(RUNTIME_TARGET_CONTROL);
}

// 三组路由的发送方向一致。各 Endpoint 使用自己的回复类型，内核按值复制。
#define TRANSPORT(prefix, Name, instance, module_actor, control_actor)                             \
    static SysResult_t prefix##_send_request(void *port_context, const LCRequest *request)         \
    {                                                                                              \
        V2RtosRoute *route = port_context;                                                         \
        return send(&route->request, RUNTIME_TARGET_CONTROL, request);                             \
    }                                                                                              \
    static SysResult_t prefix##_receive_request(void *port_context, LCRequest *request)            \
    {                                                                                              \
        V2RtosRoute *route = port_context;                                                         \
        return receive(&route->request, route->task, request);                                     \
    }                                                                                              \
    static SysResult_t prefix##_send_reply(void *port_context, const Name##Reply *reply)           \
    {                                                                                              \
        V2RtosRoute *route = port_context;                                                         \
        return send(&route->completion, route->task, reply);                                       \
    }                                                                                              \
    static SysResult_t prefix##_receive_reply(void *port_context, Name##Reply *reply)              \
    {                                                                                              \
        V2RtosRoute *route = port_context;                                                         \
        return receive(&route->completion, RUNTIME_TARGET_CONTROL, reply);                         \
    }                                                                                              \
    static SysResult_t prefix##_send_consumed(void *port_context, const LCCompletion *completion)  \
    {                                                                                              \
        V2RtosRoute *route = port_context;                                                         \
        return send(&route->consumed, RUNTIME_TARGET_CONTROL, completion);                         \
    }                                                                                              \
    static SysResult_t prefix##_receive_consumed(void *port_context, LCCompletion *completion)     \
    {                                                                                              \
        V2RtosRoute *route = port_context;                                                         \
        return receive(&route->consumed, route->task, completion);                                 \
    }                                                                                              \
    static SysResult_t prefix##_notify(void *port_context, uint32_t actor)                         \
    {                                                                                              \
        V2RtosRoute *route = port_context;                                                         \
        if (actor == module_actor && control_owner(port_context))                                  \
            return RuntimeEvent_Post(route->task, RUNTIME_EVENT_WAKE);                             \
        if (actor == control_actor && module_owner(port_context))                                  \
            return RuntimeEvent_Post(RUNTIME_TARGET_CONTROL, RUNTIME_EVENT_WAKE);                  \
        return SYS_ERR_INVALID_STATE;                                                              \
    }                                                                                              \
    static const Name##TransportOps prefix##_ops =                                                 \
    {module_owner, control_owner, prefix##_send_request, prefix##_receive_request,                 \
     prefix##_send_reply, prefix##_receive_reply, prefix##_send_consumed,                          \
     prefix##_receive_consumed, prefix##_notify};                                                  \
    const Name##Transport instance = {&prefix##_ops, &s_##prefix}

TRANSPORT(camera, VCamera, V2Rtos_Camera, VC_ACTOR_CAMERA, VC_ACTOR_SUPERVISOR);
TRANSPORT(usb, VUSB, V2Rtos_Usb, VU_ACTOR_USB, VU_ACTOR_SUPERVISOR);
TRANSPORT(storage, VStorage, V2Rtos_Storage, VST_ACTOR_STORAGE, VST_ACTOR_SUPERVISOR);
