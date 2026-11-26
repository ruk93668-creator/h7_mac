//实板验收命令生产者；只由生成的验收版 ControlTask 调用，正式产品不编入。
#include "network_board_acceptance.h"
#include "../../CM7/ApplicationV3/Tasks/supervisor.h"
#include "../../CM7/ApplicationV3/Core/app_messages.h"
#include "platform_time.h"

volatile uint32_t g_n5_command;
volatile uint32_t g_n5_cycles = 100U;
volatile uint32_t g_n5_dwell_ms = 1000U;
volatile uint32_t g_n5_joint_ms = 3600000U;
volatile uint32_t g_n5_file_number;
volatile NetworkBoardAcceptanceStatus_t g_n5_acceptance;

static uint32_t s_mode, s_started, s_mask;

//验收只提交既有监督器计划，硬件、租约和协议退休均由生产 Service 执行。
static void Acceptance_Fail(SysResult_t result)
{
    if (g_n5_acceptance.first_fault == SYS_OK)
        g_n5_acceptance.first_fault = result;
    g_n5_acceptance.phase = NETWORK_ACCEPTANCE_FAULT;
}

static void Acceptance_Start(void)
{
    SysResult_t result = AppV3Supervisor_Set(s_mask, g_n5_file_number);
    if (result == SYS_OK)
        g_n5_acceptance.phase = NETWORK_ACCEPTANCE_STARTING;
    else if (result != SYS_ERR_BUSY)
        Acceptance_Fail(result);
}

void NetworkBoardAcceptance_Poll(void)
{
    AppV3SupervisorStatus_t status;
    uint32_t now = Platform_TimeGetMs();
    SysResult_t result;
    if (!App_MessagesIsOwner(RUNTIME_TARGET_CONTROL))
        return;
    result = AppV3Supervisor_Read(&status);
    if (result != SYS_OK)
    {
        Acceptance_Fail(result);
        return;
    }
    g_n5_acceptance.active = status.active;
    g_n5_acceptance.uncertain = status.uncertain;
    if (g_n5_acceptance.phase == NETWORK_ACCEPTANCE_IDLE)
    {
        if (g_n5_command != 1U && g_n5_command != 2U)
            return;
        if (g_n5_cycles == 0U || g_n5_cycles > 1000U ||
            (g_n5_command == 2U && g_n5_file_number == 0U))
        {
            Acceptance_Fail(SYS_ERR_INVALID_ARG);
            return;
        }
        s_mode = g_n5_command;
        s_mask = APP_V3_PIPE_NETWORK;
        if (s_mode == 2U)
            s_mask |= APP_V3_PIPE_USB | APP_V3_PIPE_RECORD;
        Acceptance_Start();
        return;
    }
    if (g_n5_acceptance.phase == NETWORK_ACCEPTANCE_FAULT && g_n5_command == 3U &&
        !status.planning)
    {
        //失败证据不清除；仅允许请求生产监督器继续真实清理。
        result = AppV3Supervisor_Set(0U, 0U);
        g_n5_acceptance.cleanup_result = result;
        if (result != SYS_ERR_BUSY)
            g_n5_command = 0U;
        return;
    }
    if (g_n5_acceptance.phase == NETWORK_ACCEPTANCE_DONE ||
        g_n5_acceptance.phase == NETWORK_ACCEPTANCE_FAULT || status.planning)
        return;
    if (status.first_fault != SYS_OK || status.uncertain != 0U)
    {
        Acceptance_Fail(status.first_fault != SYS_OK ? status.first_fault : SYS_ERR_INVALID_STATE);
        return;
    }
    if (g_n5_acceptance.phase == NETWORK_ACCEPTANCE_STARTING)
    {
        if (status.active != (s_mask | APP_V3_PIPE_CAMERA))
        {
            Acceptance_Fail(SYS_ERR_PROTOCOL);
            return;
        }
        s_started = now;
        g_n5_acceptance.phase = NETWORK_ACCEPTANCE_RUNNING;
    }
    else if (g_n5_acceptance.phase == NETWORK_ACCEPTANCE_RUNNING)
    {
        g_n5_acceptance.elapsed_ms = now - s_started;
        if (g_n5_command != 3U && now - s_started <
            (s_mode == 1U ? g_n5_dwell_ms : g_n5_joint_ms))
            return;
        result = AppV3Supervisor_Set(0U, 0U);
        if (result == SYS_OK)
            g_n5_acceptance.phase = NETWORK_ACCEPTANCE_STOPPING;
        else if (result != SYS_ERR_BUSY)
            Acceptance_Fail(result);
    }
    else if (g_n5_acceptance.phase == NETWORK_ACCEPTANCE_STOPPING)
    {
        if (status.active != 0U || status.uncertain != 0U)
        {
            Acceptance_Fail(SYS_ERR_PROTOCOL);
            return;
        }
        ++g_n5_acceptance.completed;
        if (s_mode == 1U && g_n5_acceptance.completed < g_n5_cycles && g_n5_command != 3U)
            Acceptance_Start();
        else
        {
            g_n5_acceptance.phase = NETWORK_ACCEPTANCE_DONE;
            g_n5_command = 0U;
        }
    }
}

