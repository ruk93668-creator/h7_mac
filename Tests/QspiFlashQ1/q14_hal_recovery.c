#include "hal_link_test.c"

volatile int32_t g_q14_init_result, g_q14_recover_result;
volatile uint32_t g_q14_origin, g_q14_latched, g_q14_commands_absent, g_q14_attempts;

int Q14_HalRecoveryEntry(void)
{
    BspQspiFaultDiagnostics_t diag;
    TIM6->CR1 = TIM_CR1_CEN;
    TIM6->DIER = TIM_DIER_UIE;
    NVIC_EnableIRQ(TIM6_DAC_IRQn);
    NVIC_SetPriority(TIM6_DAC_IRQn, 4U);
    //仿真中保持控制器 BUSY，验证真实 HAL Timeout 及资源不得归还。
    QUADSPI->SR = QUADSPI_SR_BUSY;
    g_q14_init_result = BSP_QSPI_Init();
    g_q14_recover_result = BSP_QSPI_Recover();
    if (BSP_QSPI_GetFaultDiagnostics(&diag) != SYS_OK)
        return 1;
    g_q14_origin = diag.fault_origin;
    g_q14_latched = diag.fault_latched;
    g_q14_attempts = diag.recovery_attempts;
    g_q14_commands_absent = QUADSPI->CCR == 0U;
    return g_q14_init_result == SYS_ERR_TIMEOUT && g_q14_recover_result == SYS_ERR_IO &&
        diag.primary_result == SYS_ERR_TIMEOUT && diag.cleanup_result == SYS_ERR_IO &&
        diag.fault_origin == BSP_QSPI_FAULT_HAL_INIT && diag.fault_latched != 0U &&
        diag.recovery_attempts == 1U && diag.controller_idle_verified == 0U &&
        __HAL_RCC_QSPI_IS_CLK_ENABLED() && g_q14_commands_absent != 0U ? 0 : 1;
}
