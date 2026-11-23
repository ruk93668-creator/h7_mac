#if defined(CORE_CM7)
#include "framework_init.h"
#include "main.h"
#include "bsp_qspi.h"
#include "platform_rtos.h"
#include "FreeRTOS.h"
#include "task.h"

#define Q1_ARM_MAGIC 0x51315244UL

//下载不等于允许访问 Flash；另行授权后才可由调试器设置此 RAM 门闩。
volatile uint32_t g_q1_hw_arm;
typedef struct
{
    uint32_t version, phase, complete;
    SysResult_t boot_result, init_result, id_result, diagnostics_result;
    uint32_t id, started_ms, finished_ms, hclk_hz, rcc_d1ccipr;
    uint32_t gpiof_mode, gpiof_af0, gpiof_af1, gpiog_mode, gpiog_af0;
    uint32_t tim6_cr1, tim6_dier, tim6_irq_priority, primask, basepri;
    BspQspiDiagnostics_t diagnostics;
} Q1HardwareReport_t;
volatile Q1HardwareReport_t g_q1_hw_report;

static StaticTask_t s_test_tcb;
static StackType_t s_test_stack[768] __attribute__((aligned(8)));
#if defined(QSPI_SFDP_DIAGNOSTIC)
#include "qspi_sfdp_probe.h"
#else
#include "q12_scan.h"
#endif

//由生成器逐字提取批准基线的同名函数，不改变生产时钟配方。


static void Q1_Task(void *argument)
{
    BspQspiDiagnostics_t diagnostics;
    uint32_t id = 0U;
    (void)argument;
    g_q1_hw_report.phase = 2U;
    while (g_q1_hw_arm != Q1_ARM_MAGIC)
        vTaskDelay(pdMS_TO_TICKS(10U));
    g_q1_hw_arm = 0U;
    g_q1_hw_report.phase = 3U;
    g_q1_hw_report.started_ms = HAL_GetTick();
    //只调用已验收的四 API 中的只读识别路径，不构造第二套 HAL 命令。
    g_q1_hw_report.init_result = BSP_QSPI_Init();
    g_q1_hw_report.id_result = BSP_QSPI_GetId(&id);
    g_q1_hw_report.id = id;
    g_q1_hw_report.diagnostics_result = BSP_QSPI_GetDiagnostics(&diagnostics);
    if (g_q1_hw_report.diagnostics_result == SYS_OK)
        g_q1_hw_report.diagnostics = diagnostics;
    g_q1_hw_report.finished_ms = HAL_GetTick();
    g_q1_hw_report.hclk_hz = HAL_RCC_GetHCLKFreq();
    g_q1_hw_report.rcc_d1ccipr = RCC->D1CCIPR;
    g_q1_hw_report.gpiof_mode = GPIOF->MODER;
    g_q1_hw_report.gpiof_af0 = GPIOF->AFR[0];
    g_q1_hw_report.gpiof_af1 = GPIOF->AFR[1];
    g_q1_hw_report.gpiog_mode = GPIOG->MODER;
    g_q1_hw_report.gpiog_af0 = GPIOG->AFR[0];
    g_q1_hw_report.tim6_cr1 = TIM6->CR1;
    g_q1_hw_report.tim6_dier = TIM6->DIER;
    g_q1_hw_report.tim6_irq_priority = NVIC_GetPriority(TIM6_DAC_IRQn);
    g_q1_hw_report.primask = __get_PRIMASK();
    g_q1_hw_report.basepri = __get_BASEPRI();
    __DMB();
    g_q1_hw_report.phase = 4U;
    g_q1_hw_report.complete = 1U;
#if defined(QSPI_SFDP_DIAGNOSTIC)
    if (g_q1_hw_report.init_result == SYS_OK && id == BSP_QSPI_EXPECTED_ID)
    {
        while (g_qspi_sfdp_arm != QSPI_SFDP_ARM_MAGIC)
            vTaskDelay(pdMS_TO_TICKS(10U));
        g_qspi_sfdp_arm = 0U;
        QspiSfdp_Run();
    }
#else
    //Q1.2 必须另行授权并单独置 RAM 门闩；沿用本任务，不新增任务。
    while (g_q12_hw_arm != Q12_ARM_MAGIC)
        vTaskDelay(pdMS_TO_TICKS(10U));
    g_q12_hw_arm = 0U;
    Q12_Scan();
#endif
    //一次运行只有一次扫描；失败不自动重试。
    for (;;)
        vTaskDelay(pdMS_TO_TICKS(1000U));
}


//Initcall 只创建静态测试任务，调度启动后仍等待人工 RAM 门闩。
static SysResult_t Q1_M7TestPrepare(void)
{
    g_q1_hw_report.version = 1U;
    g_q1_hw_report.phase = 1U;
    g_q1_hw_report.boot_result = SYS_ERR_INVALID_STATE;
    if (xTaskCreateStatic(Q1_Task, "qspi_ro", 768U, NULL, 1U,
        s_test_stack, &s_test_tcb) == NULL)
        return SYS_ERR_NO_RESOURCE;
    g_q1_hw_report.boot_result = SYS_OK;
    return SYS_OK;
}

FRAMEWORK_INIT_REGISTER(Q1_M7TestPrepare, INIT_LEVEL_APPLICATION);
#endif
