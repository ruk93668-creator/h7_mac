// 在仿真中选择任务身份，使用真实 FreeRTOS 队列、TCB 和通知验证 P8.2。
#include "stm32h7xx.h"
#include "bsp_dcmi.h"
static uint32_t s_exception;
// 只替换异常编号读数，保留内核任务和队列实现。
#define __get_IPSR() s_exception
#include "../../Middlewares/Third_Party/FreeRTOS/Source/tasks.c"
#include "../../CM7/Platform/platform_rtos.c"
#include "../../Common/Src/runtime_event.c"
#include "../../CM7/Application/app_events.c"
#include "../../CM7/Application/Tasks/ipc_task.c"
#include "../../CM7/ApplicationV2/Core/Runtime/v2_rtos_runtime.h"
#include "../../CM7/ApplicationV2/Core/Runtime/v2_rtos_transport.h"
#include "../../CM7/ApplicationV2/Configuration/v2_board_v1.h"
#include "../LegacyLifecycle/Source/app_camera_qualification_internal.h"
#include <string.h>

volatile uint32_t g_failure;
volatile uint32_t g_ipc_starts, g_ipc_steps;
volatile uint32_t g_case;
uint32_t SystemCoreClock = 480000000U;
static uint32_t s_now;
static bool (*s_ipc_owner)(void *);
static void *s_ipc_port;
static StaticTask_t s_tcbs[RUNTIME_TARGET_COUNT];
static StackType_t s_stacks[RUNTIME_TARGET_COUNT][128];
static TaskHandle_t s_owners[RUNTIME_TARGET_COUNT];

#define CHECK(condition)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(condition))                                                                          \
        {                                                                                          \
            g_failure = __LINE__;                                                                  \
            return g_failure;                                                                      \
        }                                                                                          \
    } while (0)

uint32_t Platform_SyncGetException(void)
{
    return s_exception;
}
uint32_t Platform_SyncGetPrimask(void)
{
    return __get_PRIMASK();
}
uint32_t Platform_SyncGetBasepri(void)
{
    return __get_BASEPRI();
}
void Platform_SyncDisableIrq(void)
{
    __disable_irq();
}
void Platform_SyncRestoreIrq(uint32_t mask)
{
    __set_PRIMASK(mask);
}
void Platform_SyncMemoryBarrier(void)
{
    __DMB();
}
uint32_t Platform_SyncGetIrqPriority(uint32_t irq)
{
    return NVIC_GetPriority((IRQn_Type)irq);
}
uint32_t Platform_TimeGetMs(void)
{
    return s_now;
}

SysResult_t USB_Device_SetEventCallback(UsbDeviceEventCallback_t callback, void *port)
{
    (void)callback;
    (void)port;
    return SYS_OK;  // 硬件设备留在夹具之外。
}
SysResult_t BSP_DCMI_SetEventCallback(BspDcmiEventCallback_t callback, void *port)
{
    (void)callback;
    (void)port;
    return SYS_OK;
}
extern void Native_UnboundTrap(void);
SysResult_t App_CameraQualificationAccess(AppCameraQualification_t **core,
    const AppQualificationResourceOps_t **ops, bool preparing)
{
    (void)core;
    (void)ops;
    (void)preparing;
    Native_UnboundTrap();
    return SYS_ERR_INVALID_STATE;
}
bool App_MessagesIsOwner(RuntimeEventTarget_t task)
{
    return App_EventsIsOwner(task);
}

SysResult_t IPC_Service_SetRuntimeOwner(bool (*check)(void *), void *port)
{
    s_ipc_owner = check;
    s_ipc_port = port;
    return SYS_OK;
}
SysResult_t IPC_Service_RuntimeStart(uint32_t now)
{
    (void)now;
    if (!s_ipc_owner(s_ipc_port))
        return SYS_ERR_INVALID_STATE;
    ++g_ipc_starts;
    return SYS_OK;
}
SysResult_t IPC_Service_RuntimeProcess(uint32_t now)
{
    (void)now;
    if (!s_ipc_owner(s_ipc_port))
        return SYS_ERR_INVALID_STATE;
    ++g_ipc_steps;
    return SYS_OK;
}
SysResult_t App_StatusGetSnapshotAt(IpcApplicationStatus_t *status, uint32_t *sample)
{
    memset(status, 0, sizeof(*status));
    *sample = s_now;
    return SYS_OK;
}
SysResult_t IPC_Service_SetApplication(const IpcApplicationStatus_t *status, uint32_t sample)
{
    (void)status;
    (void)sample;
    return s_ipc_owner(s_ipc_port) ? SYS_OK : SYS_ERR_INVALID_STATE;
}
SysResult_t IPC_Service_GetRuntimeStatus(IpcRuntimeStatus_t *status)
{
    memset(status, 0, sizeof(*status));
    return SYS_OK;
}

static void idle(void *port)
{
    (void)port;
    // 调度夹具仅使用此任务的 TCB，测试不运行该入口。
    for (;;)
    {
    }
}

// 由仿真夹具选择当前任务；队列操作仍由 FreeRTOS 执行。
static void select_owner(RuntimeEventTarget_t role)
{
    pxCurrentTCB = s_owners[role];
}

// 用零等待读取真实通知位，便于检查多次唤醒是否合并。
static uint32_t wait_events(RuntimeEventTarget_t role)
{
    uint32_t events = 0U;
    select_owner(role);
    (void)App_EventsWait(role, 0U, &events);
    return events;
}

// 创建并登记真实 TCB，让测试控制任务选择和 BOOT 放行时机。
static uint32_t initialize(void)
{
    uint32_t index;
    CHECK(App_EventsInit() == SYS_OK);
    CHECK(V2Rtos_Init() == SYS_OK);
    CHECK(V2Rtos_Init() == SYS_ERR_INVALID_STATE);
    for (index = 0U; index < RUNTIME_TARGET_COUNT; ++index)
    {
        if (index == RUNTIME_TARGET_IPC)
        {
            CHECK(IpcTask_Create() == SYS_OK);
            s_owners[index] = s_ipc_task;
        }
        else
        {
            s_owners[index] = xTaskCreateStatic(idle, "Owner", 128U, NULL, 1U, s_stacks[index], &s_tcbs[index]);
            CHECK(s_owners[index] != NULL);
            CHECK(App_EventsAttach((RuntimeEventTarget_t)index, s_owners[index]) == SYS_OK);
        }
    }
    CHECK(App_EventsRecordInitialization(SYS_OK) == SYS_OK);
    xSchedulerRunning = pdTRUE;
    __set_BASEPRI(0U);
    select_owner(RUNTIME_TARGET_CAMERA);
    CHECK(App_EventsReleaseBoot() == SYS_ERR_INVALID_STATE);
    return 0U;
}

// 在指定任务身份下推进一次生产 Endpoint 或控制轮次。
static void run_owner_round(RuntimeEventTarget_t role)
{
    uint32_t events = wait_events(role);
    V2Runtime_Round(role, events, s_now);
}

// 三种真实内核队列逐条填满，再用同一身份重试。两个通知合并为一个位。
#define CHECK_ROUTE(link, Name, role, module, actor)                                                \
    do                                                                                            \
    {                                                                                             \
        LCRequest request = {{1U, 1U, module, LC_CONFIGURE}, LC_UNCONFIGURED, 50U};                  \
        LCRequest received;                                                                       \
        LCCompletion ack = {0};                                                                   \
        LCCompletion ack_out;                                                                     \
        Name##Reply reply = {0};                                                                  \
        Name##Reply reply_out;                                                                    \
        int phase;                                                                                \
        for (phase = 0; phase < 3; ++phase)                                                        \
        {                                                                                         \
            select_owner(role);                                                                   \
            CHECK(link.ops->send_request(link.port, &request) == SYS_ERR_INVALID_STATE);            \
            select_owner(RUNTIME_TARGET_CONTROL);                                                 \
            request.id.request = 1U;                                                              \
            ack.id = request.id;                                                                  \
            reply.completion.id = request.id;                                                     \
            if (phase == 0)                                                                       \
            {                                                                                     \
                CHECK(link.ops->send_request(link.port, &request) == SYS_OK);                       \
                CHECK(link.ops->notify(link.port, actor) == SYS_OK);                                \
                request.id.request = 2U;                                                          \
                CHECK(link.ops->send_request(link.port, &request) == SYS_OK);                       \
                CHECK(link.ops->notify(link.port, actor) == SYS_OK);                                \
                request.id.request = 3U;                                                          \
                CHECK(link.ops->send_request(link.port, &request) == SYS_ERR_NO_RESOURCE);          \
                CHECK(wait_events(role) == RUNTIME_EVENT_WAKE);                                     \
                CHECK(wait_events(role) == 0U);                                                     \
                CHECK(link.ops->receive_request(link.port, &received) == SYS_OK && received.id.request == 1U); \
                select_owner(RUNTIME_TARGET_CONTROL);                                              \
                CHECK(link.ops->send_request(link.port, &request) == SYS_OK);                       \
                select_owner(role);                                                               \
                CHECK(link.ops->receive_request(link.port, &received) == SYS_OK && received.id.request == 2U); \
                CHECK(link.ops->receive_request(link.port, &received) == SYS_OK && received.id.request == 3U); \
            }                                                                                     \
            else if (phase == 1)                                                                  \
            {                                                                                     \
                select_owner(role);                                                               \
                CHECK(link.ops->send_reply(link.port, &reply) == SYS_OK);                          \
                reply.completion.id.request = 2U;                                                 \
                CHECK(link.ops->send_reply(link.port, &reply) == SYS_OK);                          \
                reply.completion.id.request = 3U;                                                 \
                CHECK(link.ops->send_reply(link.port, &reply) == SYS_ERR_NO_RESOURCE);              \
                select_owner(RUNTIME_TARGET_CONTROL);                                             \
                CHECK(link.ops->receive_reply(link.port, &reply_out) == SYS_OK && reply_out.completion.id.request == 1U); \
                select_owner(role);                                                               \
                CHECK(link.ops->send_reply(link.port, &reply) == SYS_OK);                          \
                select_owner(RUNTIME_TARGET_CONTROL);                                             \
                CHECK(link.ops->receive_reply(link.port, &reply_out) == SYS_OK && reply_out.completion.id.request == 2U); \
                CHECK(link.ops->receive_reply(link.port, &reply_out) == SYS_OK && reply_out.completion.id.request == 3U); \
            }                                                                                     \
            else                                                                                  \
            {                                                                                     \
                CHECK(link.ops->send_consumed(link.port, &ack) == SYS_OK);                         \
                ack.id.request = 2U;                                                              \
                CHECK(link.ops->send_consumed(link.port, &ack) == SYS_OK);                         \
                ack.id.request = 3U;                                                              \
                CHECK(link.ops->send_consumed(link.port, &ack) == SYS_ERR_NO_RESOURCE);              \
                select_owner(role);                                                               \
                CHECK(link.ops->receive_consumed(link.port, &ack_out) == SYS_OK && ack_out.id.request == 1U); \
                select_owner(RUNTIME_TARGET_CONTROL);                                             \
                CHECK(link.ops->send_consumed(link.port, &ack) == SYS_OK);                         \
                select_owner(role);                                                               \
                CHECK(link.ops->receive_consumed(link.port, &ack_out) == SYS_OK && ack_out.id.request == 2U); \
                CHECK(link.ops->receive_consumed(link.port, &ack_out) == SYS_OK && ack_out.id.request == 3U); \
            }                                                                                     \
        }                                                                                         \
    } while (0)

// 按用例检查 BOOT、九队列、完成确认、补偿和 IPC 隔离。
uint32_t P82_Run(uint32_t scenario)
{
    uint32_t index;
    bool injected = false;
    g_case = scenario;
    CHECK(initialize() == 0U);
    if (scenario == 0U)
    {
        LCRequest request = {0};
        select_owner(RUNTIME_TARGET_CONTROL);
        CHECK(!V2Rtos_Enter(RUNTIME_TARGET_CONTROL, RUNTIME_EVENT_WAKE));
        CHECK(V2Rtos_Camera.ops->send_request(V2Rtos_Camera.port, &request) == SYS_ERR_INVALID_STATE);
        V2Runtime_Round(RUNTIME_TARGET_CONTROL, 0U, 0U);
        CHECK(!g_v2_runtime_debug.ready);
    }
    select_owner(RUNTIME_TARGET_CONTROL);
    CHECK(App_EventsReleaseBoot() == SYS_OK);
    if (scenario < 2U)
    {
        for (index = 0U; index < RUNTIME_TARGET_COUNT; ++index)
            CHECK(V2Rtos_Enter((RuntimeEventTarget_t)index, wait_events((RuntimeEventTarget_t)index)));
        select_owner(RUNTIME_TARGET_CONTROL);
        s_exception = 16U;
        CHECK(!V2Rtos_Owner(RUNTIME_TARGET_CONTROL));
        s_exception = 0U;
        __set_BASEPRI(0x50U);
        CHECK(!V2Rtos_Owner(RUNTIME_TARGET_CONTROL));
        __set_BASEPRI(0U);
        __disable_irq();
        CHECK(!V2Rtos_Owner(RUNTIME_TARGET_CONTROL));
        __enable_irq();
        CHECK(V2Rtos_Owner(RUNTIME_TARGET_CONTROL));
        if (scenario == 1U)
        {
            CHECK_ROUTE(V2Rtos_Camera, VCamera, RUNTIME_TARGET_CAMERA, V2_BOARD_CAMERA, VC_ACTOR_CAMERA);
            CHECK_ROUTE(V2Rtos_Usb, VUSB, RUNTIME_TARGET_USB, V2_BOARD_USB, VU_ACTOR_USB);
            CHECK_ROUTE(V2Rtos_Storage, VStorage, RUNTIME_TARGET_STORAGE, V2_BOARD_STORAGE, VST_ACTOR_STORAGE);
        }
        return 0U;
    }
    if (scenario == 5U)
    {
        select_owner(RUNTIME_TARGET_IPC);
        IpcTask_Run(NULL);  // 运行器在实际内核 vTaskDelay(100) 入口截取证据。
        return 1U;
    }
    if (scenario == 3U)
    {
        VCameraReply stale = {0};
        CHECK(V2Rtos_Enter(RUNTIME_TARGET_CAMERA, wait_events(RUNTIME_TARGET_CAMERA)));
        stale.completion.id.session = 9U;
        stale.completion.id.module = V2_BOARD_CAMERA;
        stale.completion.id.request = 1U;
        stale.completion.id.operation = LC_CONFIGURE;
        stale.completion.fact = LC_ACTIVE;
        CHECK(V2Rtos_Camera.ops->send_reply(V2Rtos_Camera.port, &stale) == SYS_OK);
        CHECK(V2Rtos_Enter(RUNTIME_TARGET_CONTROL, wait_events(RUNTIME_TARGET_CONTROL)));
        CHECK(V2Rtos_Camera.ops->send_consumed(V2Rtos_Camera.port, &stale.completion) == SYS_OK);
    }
    for (index = 0U; index < 1000U && !g_v2_runtime_debug.final; ++index)
    {
        s_now += 10U;
        run_owner_round(RUNTIME_TARGET_CONTROL);
        if (scenario == 3U && !injected && g_v2_runtime_debug.confirmed[2] == 1U)
        {
            const LCRequest duplicate = {{1U, 1U, V2_BOARD_CAMERA, LC_CONFIGURE},
                                         LC_UNCONFIGURED, 15000U};
            select_owner(RUNTIME_TARGET_CONTROL);
            CHECK(V2Rtos_Camera.ops->send_request(V2Rtos_Camera.port, &duplicate) == SYS_OK);
            injected = true;
        }
        run_owner_round(RUNTIME_TARGET_CAMERA);
        run_owner_round(RUNTIME_TARGET_USB);
        if (scenario == 4U && g_v2_runtime_debug.confirmed[2] == 2U &&
            g_v2_runtime_debug.confirmed[1] == 1U)
        {
            LCRequest request;
            select_owner(RUNTIME_TARGET_STORAGE);
            // 模拟补偿执行结果未知，真实队列保留完整身份，EM 应阻塞。
            if (V2Rtos_Storage.ops->receive_request(V2Rtos_Storage.port, &request) == SYS_OK)
            {
                VStorageReply reply = {0};
                CHECK(request.id.operation == LC_CLEANUP);
                reply.completion.id = request.id;
                reply.completion.result = SYS_ERR_IO;
                reply.completion.actual_result = SYS_ERR_IO;
                reply.completion.fact = LC_ERROR;
                CHECK(V2Rtos_Storage.ops->send_reply(V2Rtos_Storage.port, &reply) == SYS_OK);
            }
        }
        else
            run_owner_round(RUNTIME_TARGET_STORAGE);
    }
    CHECK(g_v2_runtime_debug.final == 1U);
    CHECK(scenario != 3U || injected);
    CHECK(g_v2_runtime_debug.first_error == SYS_ERR_NOT_SUPPORTED);
    if (scenario == 4U)
    {
        CHECK(g_v2_runtime_debug.execution_state == EM_BLOCKED);
        CHECK(g_v2_runtime_debug.recovery_error == SYS_ERR_IO);
    }
    else
    {
        CHECK(g_v2_runtime_debug.execution_state == EM_DONE);
        CHECK(g_v2_runtime_debug.recovery_error == SYS_OK);
        LCCompletion pending_ack;
        CHECK(g_v2_runtime_debug.confirmed[0] == 2U && g_v2_runtime_debug.confirmed[1] == 2U &&
              g_v2_runtime_debug.confirmed[2] == 3U);
        run_owner_round(RUNTIME_TARGET_CAMERA);
        CHECK(V2Rtos_Camera.ops->receive_consumed(V2Rtos_Camera.port, &pending_ack) == SYS_ERR_NOT_FOUND);
        run_owner_round(RUNTIME_TARGET_USB);
        CHECK(V2Rtos_Usb.ops->receive_consumed(V2Rtos_Usb.port, &pending_ack) == SYS_ERR_NOT_FOUND);
        run_owner_round(RUNTIME_TARGET_STORAGE);
        CHECK(V2Rtos_Storage.ops->receive_consumed(V2Rtos_Storage.port, &pending_ack) == SYS_ERR_NOT_FOUND);
    }
    return 0U;
}
