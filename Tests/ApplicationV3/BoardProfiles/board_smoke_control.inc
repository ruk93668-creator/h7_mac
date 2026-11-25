#if APP_V3_BOARD_SMOKE != APP_V3_BOARD_SMOKE_OFF
#if APP_V3_BOARD_SMOKE == APP_V3_BOARD_SMOKE_CAMERA
#include "../Services/Camera/camera_service.h"
#define BOARD_SMOKE_SERVICE RUNTIME_TARGET_CAMERA
#elif APP_V3_BOARD_SMOKE == APP_V3_BOARD_SMOKE_USB
#include "../Services/USB/usb_service.h"
#define BOARD_SMOKE_SERVICE RUNTIME_TARGET_USB
#else
#include "../Services/Storage/storage_service.h"
#define BOARD_SMOKE_SERVICE RUNTIME_TARGET_STORAGE
#endif

volatile AppV3BoardSmokeReport_t g_app_v3_board_smoke;
static uint32_t s_smoke_ticket;
static TickType_t s_smoke_started;

//用无符号Tick差值限制每步的提交与等待总时间
static bool BoardSmoke_Expired(void)
{
    return (TickType_t)(xTaskGetTickCount() - s_smoke_started) >=
        pdMS_TO_TICKS(APP_V3_BOARD_SMOKE_TIMEOUT_MS);
}

//只保存当前步骤的真实终结；同步回调不提交命令
static SysResult_t BoardSmoke_Record(const AppV3ServiceEvent_t *event, void *context)
{
    uint32_t step = g_app_v3_board_smoke.step;
    (void)context;

    //同一Service的模拟结果、其他请求者及迟到结果均不推进本测试
    if (g_app_v3_board_smoke.state == APP_V3_SMOKE_WAIT &&
        g_app_v3_board_smoke.received == step &&
        event->service == BOARD_SMOKE_SERVICE && event->requester == RUNTIME_TARGET_CONTROL &&
        event->real_hardware && event->command_id == APP_V3_BOARD_SMOKE_FIRST_ID + step &&
        !BoardSmoke_Expired())
    {
        g_app_v3_board_smoke.events[step] = *event;
#if APP_V3_BOARD_STRESS != APP_V3_BOARD_STRESS_OFF
        g_app_v3_board_smoke.latency_ticks[step] = (TickType_t)(xTaskGetTickCount() - s_smoke_started);
#endif
        g_app_v3_board_smoke.received = step + 1U;
    }
    return SYS_OK;
}

//断连后先查询FAULT，复接后经原RECOVER确认退出，再验证同一服务可重启
#if APP_V3_BOARD_STRESS == APP_V3_BOARD_STRESS_RECOVERY
typedef enum
{
    BOARD_SMOKE_START = 0,
    BOARD_SMOKE_QUERY,
    BOARD_SMOKE_STOP,
    BOARD_SMOKE_QUERY_STOPPED,
    BOARD_SMOKE_RECOVER
} BoardSmokeOperationIndex_t;

static const BoardSmokeOperationIndex_t s_recovery_operations[APP_V3_BOARD_SMOKE_STEPS] =
{
    BOARD_SMOKE_START, BOARD_SMOKE_QUERY, BOARD_SMOKE_QUERY, BOARD_SMOKE_RECOVER,
    BOARD_SMOKE_START, BOARD_SMOKE_QUERY, BOARD_SMOKE_STOP, BOARD_SMOKE_QUERY_STOPPED
};
#endif

//只在当前构建中引用一个服务；Storage使用原只读START
static SysResult_t BoardSmoke_Request(uint32_t step)
{
    uint32_t command_id = APP_V3_BOARD_SMOKE_FIRST_ID + step;
#if APP_V3_BOARD_STRESS == APP_V3_BOARD_STRESS_RECOVERY
    uint32_t operation_index = s_recovery_operations[step];
#else
    uint32_t operation_index = step % APP_V3_BOARD_SMOKE_CYCLE_STEPS;
#endif
#if APP_V3_BOARD_SMOKE == APP_V3_BOARD_SMOKE_CAMERA
    static const CameraServiceOperation_t operations[] =
    {
        CAMERA_SERVICE_START, CAMERA_SERVICE_HARDWARE_QUERY,
        CAMERA_SERVICE_STOP, CAMERA_SERVICE_HARDWARE_QUERY
    };
    return CameraService_Request(command_id, operations[operation_index], 0U);
#elif APP_V3_BOARD_SMOKE == APP_V3_BOARD_SMOKE_USB
    static const UsbServiceOperation_t operations[] =
    {
        USB_SERVICE_START, USB_SERVICE_HARDWARE_QUERY,
        USB_SERVICE_STOP, USB_SERVICE_HARDWARE_QUERY, USB_SERVICE_RECOVER
    };
    return UsbService_Request(command_id, operations[operation_index], 0U);
#else
    static const StorageServiceOperation_t operations[] =
    {
        STORAGE_SERVICE_START, STORAGE_SERVICE_HARDWARE_QUERY,
        STORAGE_SERVICE_STOP, STORAGE_SERVICE_HARDWARE_QUERY, STORAGE_SERVICE_RECOVER
    };
    return StorageService_Request(command_id, operations[operation_index], 0U);
#endif
}

//完成状态与首错要来自本步骤的真实结果，不靠等待时长推断资源释放
static SysResult_t BoardSmoke_CheckResult(uint32_t step)
{
    const volatile AppV3ServiceEvent_t *event = &g_app_v3_board_smoke.events[step];
    AppV3HardwareState_t expected;

#if APP_V3_BOARD_STRESS == APP_V3_BOARD_STRESS_RECOVERY
    if (step == 2U)
    {
        //USB查询返回快照；Storage查询仍可能直接报告卡不存在
        if (event->hardware.state != APP_V3_HW_FAULT ||
            (event->result != SYS_OK && event->result != SYS_ERR_NOT_FOUND) ||
            event->hardware.first_fault == SYS_OK)
            return SYS_ERR_PROTOCOL;
        return SYS_OK;
    }
    if (step == 3U && event->result == SYS_OK &&
        event->hardware.first_fault != g_app_v3_board_smoke.events[2].hardware.first_fault)
        return SYS_ERR_PROTOCOL;
    if (step >= 4U && event->result == SYS_OK && event->hardware.first_fault != SYS_OK)
        return event->hardware.first_fault;
    expected = (step < 2U || step == 4U || step == 5U) ? APP_V3_HW_ACTIVE : APP_V3_HW_READY;
#else
#if APP_V3_BOARD_STRESS == APP_V3_BOARD_STRESS_REPEAT
    //观察期间的异步FAULT即使后来STOP成功，也不能算无故障运行
    if (event->result == SYS_OK && event->hardware.first_fault != SYS_OK)
        return event->hardware.first_fault;
#endif
    expected = step % APP_V3_BOARD_SMOKE_CYCLE_STEPS < 2U ? APP_V3_HW_ACTIVE : APP_V3_HW_READY;
#endif
    if (event->result != SYS_OK)
        return event->result;
    return event->hardware.state == expected ? SYS_OK : SYS_ERR_PROTOCOL;
}

#if APP_V3_BOARD_SMOKE == APP_V3_BOARD_SMOKE_USB || APP_V3_BOARD_STRESS != APP_V3_BOARD_STRESS_OFF
//观察窗口只在收到上一条成功终结后开启，期间不提交下一条命令
static uint32_t BoardSmoke_ObserveMs(uint32_t next_step)
{
#if APP_V3_BOARD_STRESS == APP_V3_BOARD_STRESS_RECOVERY
    if (next_step == 2U || next_step == 3U)
        return APP_V3_BOARD_STRESS_OPERATOR_MS;
#if APP_V3_BOARD_SMOKE == APP_V3_BOARD_SMOKE_USB
    if (next_step == 6U)
        return APP_V3_BOARD_SMOKE_USB_OBSERVE_MS;
#endif
#else
    if (next_step % APP_V3_BOARD_SMOKE_CYCLE_STEPS == 2U)
    {
#if APP_V3_BOARD_STRESS == APP_V3_BOARD_STRESS_REPEAT
        if (next_step + 2U == APP_V3_BOARD_SMOKE_STEPS)
            return APP_V3_BOARD_STRESS_SOAK_MS;
#endif
#if APP_V3_BOARD_SMOKE == APP_V3_BOARD_SMOKE_USB
        return APP_V3_BOARD_SMOKE_USB_OBSERVE_MS;
#endif
    }
#endif
    return 0U;
}
#endif

//计算测试结果后撤销订阅；不清理或推断任何硬件所有权
static void BoardSmoke_Finish(AppV3BoardSmokeState_t state, SysResult_t result)
{
#if APP_V3_BOARD_STRESS != APP_V3_BOARD_STRESS_OFF
    UBaseType_t remaining_words[RUNTIME_TARGET_COUNT];
    uint32_t target_index;

    //终局只采样一次；读取失败单独保留，不能覆盖真实业务首错
    if (App_EventsReadStackWatermarks(remaining_words))
    {
        for (target_index = 0U; target_index < RUNTIME_TARGET_COUNT; ++target_index)
            g_app_v3_board_smoke.stack_remaining_words[target_index] = remaining_words[target_index];
        g_app_v3_board_smoke.stack_sampled = true;
    }
#endif
    g_app_v3_board_smoke.result = result;
    g_app_v3_board_smoke.state = state;
    if (s_smoke_ticket != 0U)
    {
        SysResult_t unsubscribe = AppV3Business_Unsubscribe(s_smoke_ticket);
        //本函数在Drain之外由订阅者Control调用，票据从未交给其他代码
        configASSERT(unsubscribe == SYS_OK);
        (void)unsubscribe;
        s_smoke_ticket = 0U;
    }
}

//只在Control完成本轮Drain后推进；每轮最多提交一个已有命令
static void BoardSmoke_Poll(void)
{
    SysResult_t result;
    uint32_t step = g_app_v3_board_smoke.step;

    if (g_app_v3_board_smoke.state == APP_V3_SMOKE_IDLE)
    {
        g_app_v3_board_smoke.service = BOARD_SMOKE_SERVICE;
        result = AppV3Business_Subscribe(BOARD_SMOKE_SERVICE, BoardSmoke_Record, NULL, &s_smoke_ticket);
        if (result != SYS_OK)
        {
            BoardSmoke_Finish(APP_V3_SMOKE_FAILED, result);
            return;
        }
        s_smoke_started = xTaskGetTickCount();
        g_app_v3_board_smoke.state = APP_V3_SMOKE_SUBMIT;
    }

    //推进下一步前同时核对原操作结果与服务报告的运行状态
    if (g_app_v3_board_smoke.state == APP_V3_SMOKE_WAIT && g_app_v3_board_smoke.received > step)
    {
        result = BoardSmoke_CheckResult(step);
        if (result != SYS_OK)
        {
            BoardSmoke_Finish(APP_V3_SMOKE_FAILED, result);
            return;
        }
        g_app_v3_board_smoke.step = ++step;
        if (step == APP_V3_BOARD_SMOKE_STEPS)
        {
            BoardSmoke_Finish(APP_V3_SMOKE_COMPLETE, SYS_OK);
            return;
        }
        s_smoke_started = xTaskGetTickCount();
#if APP_V3_BOARD_SMOKE == APP_V3_BOARD_SMOKE_USB || APP_V3_BOARD_STRESS != APP_V3_BOARD_STRESS_OFF
        if (BoardSmoke_ObserveMs(step) != 0U)
        {
            g_app_v3_board_smoke.state = APP_V3_SMOKE_OBSERVE;
            return;
        }
#endif
        g_app_v3_board_smoke.state = APP_V3_SMOKE_SUBMIT;
    }

#if APP_V3_BOARD_SMOKE == APP_V3_BOARD_SMOKE_USB || APP_V3_BOARD_STRESS != APP_V3_BOARD_STRESS_OFF
    if (g_app_v3_board_smoke.state == APP_V3_SMOKE_OBSERVE)
    {
        //沿用原Control轮询，观察期间不阻塞任务，也不提前提交STOP
        TickType_t now = xTaskGetTickCount();
        if ((TickType_t)(now - s_smoke_started) < pdMS_TO_TICKS(BoardSmoke_ObserveMs(step)))
            return;

        //窗口结束后才开始计算STOP自身的提交与完成期限
        s_smoke_started = now;
        g_app_v3_board_smoke.state = APP_V3_SMOKE_SUBMIT;
    }
#endif

    //仅未成功入队的BUSY可以重试，重试沿用原命令编号
    if (g_app_v3_board_smoke.state != APP_V3_SMOKE_SUBMIT &&
        g_app_v3_board_smoke.state != APP_V3_SMOKE_WAIT)
        return;
    if (BoardSmoke_Expired())
    {
        BoardSmoke_Finish(APP_V3_SMOKE_TIMED_OUT, SYS_ERR_TIMEOUT);
        return;
    }
    if (g_app_v3_board_smoke.state != APP_V3_SMOKE_SUBMIT)
        return;
    result = BoardSmoke_Request(step);
    if (result == SYS_OK)
        g_app_v3_board_smoke.state = APP_V3_SMOKE_WAIT;
    else if (result != SYS_ERR_BUSY)
        BoardSmoke_Finish(APP_V3_SMOKE_FAILED, result);
}
#endif
