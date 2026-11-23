#define QSPI_SFDP_DIAGNOSTIC
#include "behavior_test.c"
#include "hardware/qspi_sfdp_probe.h"

static uint32_t s_probe_case, s_opcode, s_address, s_length, s_transactions;

static HAL_StatusTypeDef ProbeCommand(QSPI_HandleTypeDef *handle,
    QSPI_CommandTypeDef *cmd, uint32_t timeout)
{
    (void)handle;
    s_transactions++;
    CHECK(timeout > 0U && timeout <= 100U && __get_PRIMASK() == 0U);
    CHECK(cmd->InstructionMode == QSPI_INSTRUCTION_1_LINE &&
          cmd->DataMode == QSPI_DATA_1_LINE && cmd->AlternateByteMode == QSPI_ALTERNATE_BYTES_NONE &&
          cmd->DdrMode == QSPI_DDR_MODE_DISABLE);
    s_opcode = cmd->Instruction;
    s_address = cmd->Address;
    s_length = cmd->NbData;
    QUADSPI->DLR = s_length - 1U;
    if (s_opcode == 0x5AU)
    {
        CHECK(s_transactions == 1U && s_address == 0U && s_length == 16U);
        CHECK(cmd->AddressMode == QSPI_ADDRESS_1_LINE &&
              cmd->AddressSize == QSPI_ADDRESS_24_BITS && cmd->DummyCycles == 8U);
        if (s_probe_case == 2U)
            return HAL_TIMEOUT;
    }
    else if (s_opcode == 0x03U || s_opcode == 0x0BU)
    {
        CHECK(cmd->AddressMode == QSPI_ADDRESS_1_LINE && cmd->AddressSize == QSPI_ADDRESS_24_BITS);
        CHECK(cmd->DummyCycles == (s_opcode == 0x03U ? 0U : 8U) && s_length == 16U);
        CHECK(s_address == 0U || s_address == 0xFF8U || s_address == 0xFFFFF0U);
    }
    else
    {
        CHECK(s_opcode == 0x05U || s_opcode == 0x35U || s_opcode == 0x15U);
        CHECK(cmd->AddressMode == QSPI_ADDRESS_NONE && cmd->DummyCycles == 0U && s_length == 1U);
    }
    return HAL_OK;
}

static HAL_StatusTypeDef ProbeReceive(QSPI_HandleTypeDef *handle, uint8_t *data, uint32_t timeout)
{
    CHECK(timeout > 0U && timeout <= 100U && __get_PRIMASK() == 0U);
    handle->RxXferSize = s_length;
    handle->RxXferCount = 0U;
    memset(data, 0, s_length);
    if (s_opcode == 0x5AU)
    {
        if (s_probe_case == 1U)
            memset(data, 0xFF, s_length);
        else
            memcpy(data, "SFDP", 4U);
        if (s_probe_case == 3U)
            handle->RxXferCount = 1U;
    }
    else if (s_opcode == 0x03U || s_opcode == 0x0BU)
    {
        memset(data, s_probe_case == 1U ? 0xFF : 0xA5, s_length);
        if (s_probe_case == 4U && s_opcode == 0x0BU)
            data[0] = 0U;
    }
    return HAL_OK;
}

int Sfdp_TestRun(uint32_t id)
{
    uint8_t data[16];
    uint32_t i;
    s_probe_case = id;
    s_hclk = 240000000U;
    TIM6->CR1 = TIM_CR1_CEN;
    TIM6->DIER = TIM_DIER_UIE;
    NVIC_EnableIRQ(TIM6_DAC_IRQn);
    NVIC_SetPriority(TIM6_DAC_IRQn, 4U);
    CHECK(BSP_QSPI_Init() == SYS_OK);
    CHECK(s_diag.serial_clock_hz == 30000000U && s_diag.prescaler == 7U);
    CHECK((QUADSPI->CR & QUADSPI_CR_SSHIFT) != 0U);
    s_read_command_hook = ProbeCommand;
    s_read_receive_hook = ProbeReceive;
    if (id == 5U)
    {
        CHECK(QspiDiagnostic_ReadSfdp(NULL) == SYS_ERR_INVALID_ARG);
        s_diag.serial_clock_hz = 120000000U;
        CHECK(QspiDiagnostic_ReadSfdp(data) == SYS_ERR_INVALID_STATE);
        CHECK(s_transactions == 0U && s_diag.fault == 0U);
    }
    else if (id == 2U || id == 3U)
    {
        memset(data, 0xCC, sizeof(data));
        CHECK(QspiDiagnostic_ReadSfdp(data) == (id == 2U ? SYS_ERR_TIMEOUT : SYS_ERR_PROTOCOL));
        CHECK(s_diag.fault == 1U && s_diag.read_completed == 0U && s_transactions == 1U);
        for (i = 0U; i < sizeof(data); i++)
            CHECK(data[i] == 0xCCU);
    }
    else
    {
        QspiSfdp_Run();
        CHECK(g_qspi_sfdp_report.complete == 1U && g_qspi_sfdp_report.phase == 3U);
        CHECK(g_qspi_sfdp_report.signature_ok == (id == 1U ? 0U : 1U));
        CHECK(g_qspi_sfdp_report.sfdp_result == SYS_OK && g_qspi_sfdp_report.samples_attempted == 3U);
        CHECK(g_qspi_sfdp_report.sfdp_diagnostics.read_opcode == 0x5AU);
        CHECK(s_transactions == 25U);
        for (i = 0U; i < 3U; i++)
        {
            CHECK(g_qspi_sfdp_report.samples[i].confirmed_03 == 16U);
            CHECK(g_qspi_sfdp_report.samples[i].confirmed_0b == 16U);
            CHECK(g_qspi_sfdp_report.samples[i].differences == (id == 4U ? 1U : 0U));
            CHECK(g_qspi_sfdp_report.samples[i].non_ff_03 == (id == 1U ? 0U : 16U));
        }
    }
    return (int)g_failure;
}
