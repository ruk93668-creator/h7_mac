//只测试独立线程门禁与阶段，BSP硬件契约由原专项另行回归。
#include "coexistence.c"
#include "ipc_application.h"
#include "../Runtime/service_mailbox.h"

//用真实头文件冻结产品消息、服务命令和跨核载荷布局。
typedef char CoexistMessageAbi[(sizeof(AppMessage_t) == 52U) ? 1 : -1];
typedef char CoexistCommandAbi[(sizeof(AppV3ServiceCommand_t) == 16U) ? 1 : -1];
typedef char CoexistIpcAbi[(sizeof(IpcApplicationStatus_t) == 80U) ? 1 : -1];

uint32_t g_assertions, g_failure;
static uint32_t s_case, s_tick, s_calls, s_inits, s_reads, s_delays, s_created;
static bool s_ready;
#define CHECK(x) do { g_assertions++; if (!(x) && !g_failure) g_failure = __LINE__; } while (0)

uint32_t HAL_GetTick(void) { return s_tick++; }
bool AppV3_BusinessReady(void) { return s_ready; }
TaskHandle_t xTaskCreateStatic(TaskFunction_t fn, const char *name, uint32_t depth,
    void *argument, UBaseType_t priority, StackType_t *stack, StaticTask_t *tcb)
{
    s_created++;
    CHECK(fn == Coexist_Task && depth == 768U && priority == 1U);
    CHECK(stack == s_coexist_stack && tcb == &s_coexist_tcb && name != NULL && argument == NULL);
    return s_case == 15U ? NULL : (TaskHandle_t)tcb;
}
void vTaskDelay(TickType_t ticks)
{
    s_delays++;
    CHECK(ticks == pdMS_TO_TICKS(10U) || ticks == pdMS_TO_TICKS(1000U));
}
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t handle)
{
    CHECK(handle == NULL);
    return 600U;
}
SysResult_t BSP_QSPI_Init(void)
{
    s_calls++; s_inits++;
    CHECK(s_ready && g_qspi_coexist_report.consumed == 1U && g_qspi_coexist_arm == 0U);
    return s_case == 4U ? SYS_ERR_IO : (s_case == 11U ? SYS_ERR_BUSY : SYS_OK);
}
SysResult_t BSP_QSPI_GetId(uint32_t *id)
{
    s_calls++;
    *id = s_case == 5U ? 0U : BSP_QSPI_EXPECTED_ID;
    return SYS_OK;
}
SysResult_t BSP_QSPI_ReadStatus(BspQspiStatus_t *status, uint32_t timeout)
{
    s_calls++;
    CHECK(timeout == 20U);
    memset(status, 0, sizeof(*status));
    status->qe = s_case == 6U ? 0U : 1U;
    status->wel = s_case == 7U ? 1U : 0U;
    return SYS_OK;
}
SysResult_t BSP_QSPI_Read(BspQspiReadMode_t mode, uint32_t address,
    void *buffer, uint32_t size, uint32_t *completed, uint32_t timeout)
{
    uint32_t i;
    s_calls++; s_reads++;
    CHECK(address == 0xFFE000U && size == 256U && timeout == 20U);
    CHECK(mode == (s_reads == 1U ? BSP_QSPI_READ_FAST : BSP_QSPI_READ_QUAD_OUTPUT));
    *completed = s_case == 9U ? 17U : size;
    for (i = 0; i < size; i++)
        ((uint8_t *)buffer)[i] = (uint8_t)(i * 73U + (i >> 3));
    if (s_case == 10U && s_reads == 2U)
        ((uint8_t *)buffer)[13] ^= 0x04U;
    return s_case == 8U ? SYS_ERR_TIMEOUT : SYS_OK;
}
SysResult_t BSP_QSPI_GetDiagnostics(BspQspiDiagnostics_t *diag)
{
    memset(diag, 0, sizeof(*diag));
    diag->ready = s_case == 4U ? 0U : 1U;
    return SYS_OK;
}
SysResult_t BSP_QSPI_GetFaultDiagnostics(BspQspiFaultDiagnostics_t *diag)
{
    memset(diag, 0, sizeof(*diag));
    diag->fault_latched = s_case == 4U ? 1U : 0U;
    diag->primary_result = s_case == 4U ? SYS_ERR_IO : SYS_OK;
    return SYS_OK;
}

//每次仿真使用干净RAM，不修改任何BSP内部故障状态。
uint32_t Coexist_TestRun(uint32_t scenario)
{
    uint32_t i, calls;
    s_case = scenario;
    CHECK(Coexist_Prepare() == (scenario == 15U ? SYS_ERR_NO_RESOURCE : SYS_OK));
    CHECK(s_created == 1U && s_calls == 0U && g_qspi_coexist_arm == 0U);
    if (scenario == 15U)
        return g_failure;
    if (scenario == 0U)
    {
        for (i = 0; i < 5U; i++) Coexist_Step();
        CHECK(s_calls == 0U && g_qspi_coexist_report.complete == 0U);
        CHECK(s_delays == 5U);
        return g_failure;
    }
    if (scenario == 1U || scenario == 13U)
    {
        s_tick = scenario == 13U ? 0xFFFFFF00U : 0U;
        s_wait_started = s_tick;
        g_qspi_coexist_arm = COEXIST_ARM;
        s_tick += 2100U;
        Coexist_Step();
        CHECK(s_calls == 0U && g_qspi_coexist_report.complete == 1U);
        CHECK(g_qspi_coexist_report.result == SYS_ERR_TIMEOUT && !g_qspi_coexist_report.consumed);
        return g_failure;
    }
    if (scenario == 14U)
    {
        g_qspi_coexist_arm = COEXIST_ARM;
        Coexist_Step();
        CHECK(s_calls == 0U && !g_qspi_coexist_report.consumed);
        g_qspi_coexist_arm = 0U;
    }
    s_ready = true;
    Coexist_Step();
    CHECK(s_calls == 0U && g_qspi_coexist_report.phase == COEXIST_WAIT_ARM);
    if (scenario == 2U)
    {
        g_qspi_coexist_arm = COEXIST_ARM ^ 1U;
        Coexist_Step();
        CHECK(s_calls == 0U && !g_qspi_coexist_report.consumed);
        return g_failure;
    }
    if (scenario == 12U)
    {
        s_ready = false;
        Coexist_Step();
        CHECK(s_calls == 0U && g_qspi_coexist_report.result == SYS_ERR_INVALID_STATE);
        return g_failure;
    }
    g_qspi_coexist_arm = COEXIST_ARM;
    for (i = 0; i < 5U; i++) Coexist_Step();
    CHECK(s_inits == 1U && g_qspi_coexist_report.consumed == 1U);
    CHECK(g_qspi_coexist_report.complete == 1U && g_qspi_coexist_report.stack_words == 600U);
    if (scenario == 3U || scenario == 14U)
        CHECK(g_qspi_coexist_report.result == SYS_OK && s_reads == 2U);
    else
        CHECK(g_qspi_coexist_report.result != SYS_OK);
    if (scenario == 9U) CHECK(g_qspi_coexist_report.fast_completed == 17U && s_reads == 1U);
    if (scenario == 10U) CHECK(g_qspi_coexist_report.differences == 1U);
    if (scenario == 4U) CHECK(g_qspi_coexist_report.fault.fault_latched == 1U);
    calls = s_calls;
    g_qspi_coexist_arm = COEXIST_ARM;
    for (i = 0; i < 4U; i++) Coexist_Step();
    CHECK(s_calls == calls && s_inits == 1U);
    return g_failure;
}
