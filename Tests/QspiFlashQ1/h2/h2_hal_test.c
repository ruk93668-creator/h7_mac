#include "h2.h"

volatile uint32_t g_h2_hal_assertions, g_h2_hal_failure;
uint32_t SystemCoreClock = 480000000U;
#define CHECK(x) do { g_h2_hal_assertions++; if (!(x) && g_h2_hal_failure == 0U) g_h2_hal_failure = __LINE__; } while (0)

uint32_t HAL_GetTick(void)
{
    static uint32_t tick;
    return tick++;
}

uint32_t HAL_RCC_GetHCLKFreq(void)
{
    return 240000000U;
}

int H2_HalEntry(void)
{
    QSPI_HandleTypeDef handle = {0};
    QSPI_CommandTypeDef command = {0};
    uint8_t bytes[H2_PROBE_BYTES];
    uint32_t i, ccr;
    H2_Boot();
    handle.Instance = QUADSPI;
    handle.State = HAL_QSPI_STATE_READY;
    command.Instruction = 0x0BU;
    command.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    command.Address = H2_FIRST;
    command.AddressMode = QSPI_ADDRESS_1_LINE;
    command.AddressSize = QSPI_ADDRESS_24_BITS;
    command.DataMode = QSPI_DATA_1_LINE;
    command.NbData = sizeof(bytes);
    command.DummyCycles = 8U;
    //只在 Unicorn 中准备 FIFO/完成标志；这些代码不会进入板上镜像。
    QUADSPI->SR = QUADSPI_SR_FTF | QUADSPI_SR_TCF;
    QUADSPI->DR = 0x5CU;
    g_qspi_h2_report.phase = H2_INJECT;
    CHECK(H2_Command(&handle, &command, 100U) == HAL_ERROR && QUADSPI->CCR == 0U);
    g_qspi_h2_report.consumed = 1U;
    CHECK(H2_Command(&handle, &command, 100U) == HAL_OK);
    CHECK(H2_Receive(&handle, bytes, 100U) == HAL_ERROR);
    CHECK(g_qspi_h2_report.rx_original == HAL_OK && g_qspi_h2_report.rx_returned == HAL_ERROR);
    CHECK(g_qspi_h2_report.rx_remaining == 0U && g_qspi_h2_report.rx_size == sizeof(bytes));
    CHECK(g_qspi_h2_report.rx_state == HAL_QSPI_STATE_READY && g_qspi_h2_report.injected == 1U);
    CHECK(handle.ErrorCode == HAL_QSPI_ERROR_NONE);
    for (i = 0U; i < sizeof(bytes); i++)
        CHECK(bytes[i] == 0x5CU);
    CHECK(H2_Command(&handle, &command, 100U) == HAL_OK);
    CHECK(H2_Receive(&handle, bytes, 100U) == HAL_OK && g_qspi_h2_report.injected == 1U);
    g_qspi_h2_report.phase = H2_RECOVER;
    command.Instruction = 0x35U;
    command.AddressMode = QSPI_ADDRESS_NONE;
    command.DummyCycles = 0U;
    command.NbData = 1U;
    CHECK(H2_Command(&handle, &command, 100U) == HAL_OK);
    CHECK(H2_Receive(&handle, bytes, 100U) == HAL_OK && bytes[0] == 0x5CU);
    ccr = QUADSPI->CCR;
    command.Instruction = 0x06U;
    CHECK(H2_Command(&handle, &command, 100U) == HAL_ERROR && QUADSPI->CCR == ccr);
    return (int)g_h2_hal_failure;
}
