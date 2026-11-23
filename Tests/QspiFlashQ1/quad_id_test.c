#include "behavior_test.c"
#include "hardware/qspi_quad_id_probe.h"

static uint32_t s_trial_case, s_last_opcode, s_quad_commands, s_status_commands;
static uint32_t s_dummy_cycles;

static HAL_StatusTypeDef QuadCommand(QSPI_HandleTypeDef *handle, QSPI_CommandTypeDef *cmd,
    uint32_t timeout)
{
    (void)handle;
    CHECK(timeout > 0U && timeout <= 100U && __get_PRIMASK() == 0U);
    CHECK(cmd->InstructionMode == QSPI_INSTRUCTION_1_LINE);
    CHECK(cmd->DdrMode == QSPI_DDR_MODE_DISABLE && cmd->SIOOMode == QSPI_SIOO_INST_EVERY_CMD);
    s_last_opcode = cmd->Instruction;
    QUADSPI->DLR = cmd->NbData - 1U;
    if (s_last_opcode == 0x94U)
    {
        s_quad_commands++;
        CHECK(s_status_commands == 3U * s_quad_commands);
        CHECK(cmd->Address == 0U && cmd->AddressMode == QSPI_ADDRESS_4_LINES &&
            cmd->AddressSize == QSPI_ADDRESS_24_BITS);
        CHECK(cmd->AlternateByteMode == QSPI_ALTERNATE_BYTES_4_LINES &&
            cmd->AlternateBytesSize == QSPI_ALTERNATE_BYTES_8_BITS && cmd->AlternateBytes == 0xF0U);
        CHECK(cmd->DummyCycles == 4U && cmd->DataMode == QSPI_DATA_4_LINES && cmd->NbData == 2U);
        s_dummy_cycles = cmd->DummyCycles;
        if (s_trial_case == 2U)
            s_dummy_cycles++;
        if (s_trial_case == 3U)
            return HAL_TIMEOUT;
    }
    else
    {
        static const uint8_t status_sequence[] = {0x05U, 0x35U, 0x15U};
        CHECK(s_last_opcode == status_sequence[s_status_commands % 3U]);
        CHECK(cmd->AddressMode == QSPI_ADDRESS_NONE && cmd->AlternateByteMode == QSPI_ALTERNATE_BYTES_NONE);
        CHECK(cmd->DummyCycles == 0U && cmd->DataMode == QSPI_DATA_1_LINE && cmd->NbData == 1U);
        s_status_commands++;
    }
    return HAL_OK;
}

static uint8_t ExchangeLanes(uint8_t nibble, uint8_t a, uint8_t b)
{
    uint8_t difference = ((nibble >> a) ^ (nibble >> b)) & 1U;
    return nibble ^ (uint8_t)((difference << a) | (difference << b));
}

static HAL_StatusTypeDef QuadReceive(QSPI_HandleTypeDef *handle, uint8_t *data, uint32_t timeout)
{
    static const uint8_t flash_symbols[] = {0xEU, 0xFU, 0x1U, 0x7U};
    uint8_t samples[4];
    uint32_t i;
    CHECK(timeout > 0U && timeout <= 100U && __get_PRIMASK() == 0U);
    handle->RxXferSize = QUADSPI->DLR + 1U;
    handle->RxXferCount = 0U;
    if (s_last_opcode != 0x94U)
    {
        data[0] = s_last_opcode == 0x35U && s_trial_case != 5U ? 2U : 0U;
        if (s_last_opcode == 0x05U && s_trial_case == 8U)
            data[0] = 1U;
        if (s_last_opcode == 0x35U && s_trial_case == 9U)
            data[0] |= 0x80U;
        return HAL_OK;
    }
    for (i = 0U; i < 4U; i++)
    {
        samples[i] = flash_symbols[(i + s_dummy_cycles - 4U) % 4U];
        if (s_trial_case == 1U)
            samples[i] = ExchangeLanes(samples[i], 0U, 3U);
        if (s_trial_case == 7U)
            samples[i] = ExchangeLanes(samples[i], 1U, 2U);
    }
    data[0] = (uint8_t)((samples[0] << 4U) | samples[1]);
    data[1] = (uint8_t)((samples[2] << 4U) | samples[3]);
    if (s_trial_case == 4U)
        return HAL_TIMEOUT;
    if (s_trial_case == 6U)
        handle->RxXferCount = 1U;
    return HAL_OK;
}

int QuadId_TestRun(uint32_t id)
{
    uint8_t output[] = {0xCCU, 0xCCU};
    SysResult_t expected;
    uint32_t i;
    s_trial_case = id;
    s_hclk = 240000000U;
    TIM6->CR1 = TIM_CR1_CEN;
    TIM6->DIER = TIM_DIER_UIE;
    NVIC_EnableIRQ(TIM6_DAC_IRQn);
    NVIC_SetPriority(TIM6_DAC_IRQn, 4U);
    CHECK(BSP_QSPI_Init() == SYS_OK);
    CHECK(s_diag.serial_clock_hz == QSPI_QUAD_ID_DIAGNOSTIC);
    CHECK(s_diag.prescaler == 240000000U / QSPI_QUAD_ID_DIAGNOSTIC - 1U);
    s_read_command_hook = QuadCommand;
    s_read_receive_hook = QuadReceive;
    if (id == 10U)
    {
        CHECK(QspiDiagnostic_ReadQuadId(NULL) == SYS_ERR_INVALID_ARG);
        s_diag.serial_clock_hz--;
        CHECK(QspiDiagnostic_ReadQuadId(output) == SYS_ERR_INVALID_STATE);
        CHECK(s_quad_commands == 0U && s_status_commands == 0U);
    }
    else if (id == 3U || id == 4U || id == 6U)
    {
        CHECK(QspiDiagnostic_ReadQuadId(output) == (id == 6U ? SYS_ERR_PROTOCOL : SYS_ERR_TIMEOUT));
        CHECK(output[0] == 0xCCU && output[1] == 0xCCU);
        CHECK(s_diag.fault == 1U && s_diag.read_completed == 0U && s_quad_commands == 1U);
    }
    else if (id == 5U || id == 8U || id == 9U)
    {
        expected = id == 5U ? SYS_ERR_NOT_SUPPORTED :
            (id == 8U ? SYS_ERR_BUSY : SYS_ERR_INVALID_STATE);
        CHECK(QspiDiagnostic_ReadQuadId(output) == expected);
        CHECK(s_quad_commands == 0U && s_status_commands == 3U && s_diag.fault == 0U);
        CHECK(output[0] == 0xCCU && output[1] == 0xCCU);
    }
    else
    {
        QspiQuadId_Run();
        CHECK(g_qspi_quad_id_report.complete == 1U && g_qspi_quad_id_report.phase == 2U);
        CHECK(g_qspi_quad_id_report.attempted == 8U && s_quad_commands == 8U);
        CHECK(g_qspi_quad_id_report.matched == (id == 0U || id == 7U ? 8U : 0U));
        for (i = 0U; i < 8U; i++)
        {
            CHECK(g_qspi_quad_id_report.trials[i].result == SYS_OK);
            CHECK(g_qspi_quad_id_report.trials[i].diagnostics.read_completed == 2U);
            CHECK(g_qspi_quad_id_report.trials[i].diagnostics.status.qe == 1U);
            CHECK(g_qspi_quad_id_report.trials[i].id[0] == (id == 1U ? 0x7FU : (id == 2U ? 0xF1U : 0xEFU)));
            CHECK(g_qspi_quad_id_report.trials[i].id[1] == (id == 1U ? 0x8EU : (id == 2U ? 0x7EU : 0x17U)));
        }
    }
    return (int)g_failure;
}
