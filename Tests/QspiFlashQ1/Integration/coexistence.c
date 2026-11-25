#include "framework_init.h"
#include "bsp_qspi.h"
#include "app_services.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stm32h7xx_hal.h"
#include <string.h>

//只有独立测试目标登记此文件，正式产品没有门闩或测试任务。
#define COEXIST_ARM 0x51313541UL
//每次调用共享短读期限，不在业务线程插入轮询。
#define COEXIST_READ_MS 20U
//启动聚合不成功时停止，不影响Control自身的一秒期限。
#define COEXIST_STARTUP_MS 2000U
//只读历史参考区，不代表重新授权擦写。
#define COEXIST_ADDRESS 0x00FFE000UL

typedef enum
{
    COEXIST_WAIT_READY = 0,
    COEXIST_WAIT_ARM,
    COEXIST_INITIALIZE,
    COEXIST_FAST,
    COEXIST_QUAD,
    COEXIST_COMPLETE,
    COEXIST_FAILED
} CoexistPhase_t;

//报告只由本测试任务写入，现场暂停后导出。
typedef struct
{
    uint32_t version, size, phase, complete, consumed;
    SysResult_t result, init_result, id_result, status_result;
    SysResult_t fast_result, quad_result, diagnostics_result, fault_result;
    uint32_t started_ms, elapsed_ms, id, fast_completed, quad_completed;
    uint32_t differences, stack_words, primask, basepri, tick_before, tick_after;
    BspQspiStatus_t status;
    BspQspiDiagnostics_t diagnostics;
    BspQspiFaultDiagnostics_t fault;
} CoexistReport_t;

//调试器只能申请一次读取，复写门闩不重复执行。
volatile uint32_t g_qspi_coexist_arm;
//固定版本与大小用于现场工具校验。
volatile CoexistReport_t g_qspi_coexist_report;
//静态TCB和栈不借用五个正式业务任务的资源。
static StaticTask_t s_coexist_tcb;
static StackType_t s_coexist_stack[768] __attribute__((aligned(8)));
//两路缓冲均为内部RAM，完整成功后才比较。
static uint8_t s_fast[256], s_quad[256];
//启动期限按无符号差处理Tick回绕。
static uint32_t s_wait_started;

//任何失败只保存一次诊断，绝不自动Init或Recover。
static void Coexist_Finish(SysResult_t result)
{
    BspQspiDiagnostics_t diagnostics;
    BspQspiFaultDiagnostics_t fault;
    g_qspi_coexist_report.result = result;
    g_qspi_coexist_report.diagnostics_result = BSP_QSPI_GetDiagnostics(&diagnostics);
    if (g_qspi_coexist_report.diagnostics_result == SYS_OK)
        g_qspi_coexist_report.diagnostics = diagnostics;
    g_qspi_coexist_report.fault_result = BSP_QSPI_GetFaultDiagnostics(&fault);
    if (g_qspi_coexist_report.fault_result == SYS_OK)
        g_qspi_coexist_report.fault = fault;
    g_qspi_coexist_report.elapsed_ms = HAL_GetTick() - g_qspi_coexist_report.started_ms;
    g_qspi_coexist_report.phase = result == SYS_OK ? COEXIST_COMPLETE : COEXIST_FAILED;
    __DMB();
    g_qspi_coexist_report.complete = 1U;
}

//每次只推进一个阶段，调用者始终在阶段间让出CPU。
static void Coexist_Poll(void)
{
    SysResult_t result;
    uint32_t completed = 0U, id = 0U, i;
    BspQspiStatus_t status;
    if (g_qspi_coexist_report.complete != 0U)
        return;
    if (g_qspi_coexist_report.phase == COEXIST_WAIT_READY)
    {
        if (!AppV3_BusinessReady())
        {
            if (HAL_GetTick() - s_wait_started >= COEXIST_STARTUP_MS)
            {
                //未初始化硬件时不查询BSP，不访问QSPI外设。
                g_qspi_coexist_report.result = SYS_ERR_TIMEOUT;
                g_qspi_coexist_report.phase = COEXIST_FAILED;
                g_qspi_coexist_report.complete = 1U;
            }
            return;
        }
        g_qspi_coexist_report.phase = COEXIST_WAIT_ARM;
    }
    if (g_qspi_coexist_report.phase == COEXIST_WAIT_ARM)
    {
        if (!AppV3_BusinessReady())
        {
            g_qspi_coexist_report.result = SYS_ERR_INVALID_STATE;
            g_qspi_coexist_report.phase = COEXIST_FAILED;
            g_qspi_coexist_report.complete = 1U;
            return;
        }
        if (g_qspi_coexist_arm != COEXIST_ARM)
            return;
        g_qspi_coexist_arm = 0U;
        g_qspi_coexist_report.consumed = 1U;
        g_qspi_coexist_report.started_ms = HAL_GetTick();
        g_qspi_coexist_report.phase = COEXIST_INITIALIZE;
        return;
    }
    if (g_qspi_coexist_report.phase == COEXIST_INITIALIZE)
    {
        g_qspi_coexist_report.primask = __get_PRIMASK();
        g_qspi_coexist_report.basepri = __get_BASEPRI();
        g_qspi_coexist_report.tick_before = HAL_GetTick();
        result = BSP_QSPI_Init();
        g_qspi_coexist_report.init_result = result;
        if (result == SYS_OK)
        {
            result = BSP_QSPI_GetId(&id);
            g_qspi_coexist_report.id_result = result;
            g_qspi_coexist_report.id = id;
            if (result == SYS_OK && id != BSP_QSPI_EXPECTED_ID)
                result = SYS_ERR_PROTOCOL;
        }
        if (result == SYS_OK)
        {
            result = BSP_QSPI_ReadStatus(&status, COEXIST_READ_MS);
            g_qspi_coexist_report.status_result = result;
            if (result == SYS_OK)
            {
                g_qspi_coexist_report.status = status;
                if (status.busy != 0U || status.sus != 0U || status.wel != 0U || status.qe == 0U)
                    result = SYS_ERR_INVALID_STATE;
            }
        }
        g_qspi_coexist_report.tick_after = HAL_GetTick();
        if (result != SYS_OK)
            Coexist_Finish(result);
        else
            g_qspi_coexist_report.phase = COEXIST_FAST;
        return;
    }
    if (g_qspi_coexist_report.phase == COEXIST_FAST)
    {
        result = BSP_QSPI_Read(BSP_QSPI_READ_FAST, COEXIST_ADDRESS,
            s_fast, sizeof(s_fast), &completed, COEXIST_READ_MS);
        g_qspi_coexist_report.fast_result = result;
        g_qspi_coexist_report.fast_completed = completed;
        if (result == SYS_OK && completed != sizeof(s_fast))
            result = SYS_ERR_PROTOCOL;
        if (result != SYS_OK)
            Coexist_Finish(result);
        else
            g_qspi_coexist_report.phase = COEXIST_QUAD;
        return;
    }
    if (g_qspi_coexist_report.phase == COEXIST_QUAD)
    {
        result = BSP_QSPI_Read(BSP_QSPI_READ_QUAD_OUTPUT, COEXIST_ADDRESS,
            s_quad, sizeof(s_quad), &completed, COEXIST_READ_MS);
        g_qspi_coexist_report.quad_result = result;
        g_qspi_coexist_report.quad_completed = completed;
        if (result == SYS_OK && completed != sizeof(s_quad))
            result = SYS_ERR_PROTOCOL;
        if (result == SYS_OK)
        {
            for (i = 0U; i < sizeof(s_fast); i++)
                if (s_fast[i] != s_quad[i])
                    g_qspi_coexist_report.differences++;
            if (g_qspi_coexist_report.differences != 0U)
                result = SYS_ERR_PROTOCOL;
        }
        Coexist_Finish(result);
    }
}

//一次推进后无条件让出，完成或失败后保持静止。
static void Coexist_Step(void)
{
    Coexist_Poll();
    g_qspi_coexist_report.stack_words = uxTaskGetStackHighWaterMark(NULL);
    vTaskDelay(pdMS_TO_TICKS(g_qspi_coexist_report.complete ? 1000U : 10U));
}

//独立线程只读取就绪状态，不登记Runtime身份、不发送业务命令。
static void Coexist_Task(void *argument)
{
    (void)argument;
    s_wait_started = HAL_GetTick();
    for (;;)
        Coexist_Step();
}

//Initcall只创建测试线程，不初始化或读取QSPI。
static SysResult_t Coexist_Prepare(void)
{
    g_qspi_coexist_report.version = 1U;
    g_qspi_coexist_report.size = sizeof(CoexistReport_t);
    g_qspi_coexist_report.result = SYS_ERR_INVALID_STATE;
    return xTaskCreateStatic(Coexist_Task, "qspi_coexist", 768U, NULL, 1U,
        s_coexist_stack, &s_coexist_tcb) == NULL ? SYS_ERR_NO_RESOURCE : SYS_OK;
}

FRAMEWORK_INIT_REGISTER(Coexist_Prepare, INIT_LEVEL_APPLICATION);
