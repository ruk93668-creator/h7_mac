#include "hal_link_test.c"

uint32_t g_q13_program_result, g_q13_erase_result, g_q13_gate_tick_calls;

int Q13_HalGateEntry(void)
{
    static const uint8_t buffer[BSP_QSPI_SECTOR_BYTES] = {0};
    BspQspiWriteDiagnostics_t diag;
    g_q13_program_result = (uint32_t)BSP_QSPI_Program(0U, buffer, 16U, 2000U, &diag);
    if (diag.result != SYS_ERR_NOT_SUPPORTED || diag.completed != 0U)
        return 1;
    g_q13_erase_result = (uint32_t)BSP_QSPI_EraseSector(0U, buffer, sizeof(buffer), 2000U, &diag);
    g_q13_gate_tick_calls = s_tick;
    return diag.result == SYS_ERR_NOT_SUPPORTED && diag.completed == 0U && s_tick == 0U &&
        g_q13_program_result == (uint32_t)SYS_ERR_NOT_SUPPORTED &&
        g_q13_erase_result == (uint32_t)SYS_ERR_NOT_SUPPORTED ? 0 : 1;
}
