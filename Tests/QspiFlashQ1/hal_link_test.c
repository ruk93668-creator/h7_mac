#include "bsp_qspi.h"
#include "stm32h7xx_hal.h"

static uint32_t s_tick;
uint32_t SystemCoreClock = 480000000U;
uint32_t g_timeout_result, g_timeout_value, g_timeout_elapsed, g_timeout_error;

//仅为独立链接/真实 HAL 超时仿真提供时钟源，不替代生产 HAL QSPI。
uint32_t HAL_GetTick(void)
{
    return s_tick++;
}

uint32_t HAL_RCC_GetHCLKFreq(void)
{
    return 240000000U;
}

int Q1_LinkEntry(void)
{
    BspQspiDiagnostics_t diag;
    BspQspiStatus_t status;
    uint8_t bytes[4];
    uint32_t completed;
    uint32_t id;
    SysResult_t result = BSP_QSPI_Init();
    if (result != SYS_OK)
        return (int)result;
    result = BSP_QSPI_GetId(&id);
    if (result != SYS_OK)
        return (int)result;
    result = BSP_QSPI_GetDiagnostics(&diag);
    if (result != SYS_OK)
        return (int)result;
    result = BSP_QSPI_ReadStatus(&status, 1000U);
    if (result != SYS_OK)
        return (int)result;
    result = BSP_QSPI_Read(BSP_QSPI_READ_FAST, 0U, bytes, sizeof(bytes), &completed, 1000U);
    if (result != SYS_OK)
        return (int)result;
    result = BSP_QSPI_Read(BSP_QSPI_READ_QUAD_OUTPUT, 0U, bytes, sizeof(bytes), &completed, 1000U);
    if (result != SYS_OK)
        return (int)result;
    return (int)BSP_QSPI_DeInit();
}

int Q1_HalTimeoutEntry(void)
{
    QSPI_HandleTypeDef handle = {0};
    handle.Instance = QUADSPI;
    handle.Init.ClockPrescaler = 7U;
    handle.Init.FifoThreshold = 1U;
    handle.Init.FlashSize = 23U;
    handle.Init.ChipSelectHighTime = QSPI_CS_HIGH_TIME_8_CYCLE;
    handle.Init.ClockMode = QSPI_CLOCK_MODE_0;
    handle.Init.FlashID = QSPI_FLASH_ID_1;
    handle.Init.DualFlash = QSPI_DUALFLASH_DISABLE;
    handle.Timeout = 1U;
    QUADSPI->SR = QUADSPI_SR_BUSY;
    g_timeout_result = HAL_QSPI_Init(&handle);
    g_timeout_value = handle.Timeout;
    g_timeout_elapsed = s_tick;
    g_timeout_error = handle.ErrorCode;
    return g_timeout_result == HAL_ERROR && (g_timeout_error & HAL_QSPI_ERROR_TIMEOUT) != 0U && g_timeout_value == 5000U &&
        g_timeout_elapsed >= 5000U && g_timeout_elapsed < 5010U ? 0 : 1;
}
