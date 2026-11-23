//生产实现 + 真实 CMSIS 布局；仅 HAL 事务由非均匀 Flash 模型替代。
#include "behavior_test.c"

static uint8_t s_output[4098], s_reference[4096];
static QSPI_CommandTypeDef s_cmd;
static uint32_t s_reads, s_data_commands, s_fault, s_at, s_calls, s_delay;
static uint32_t s_sr1, s_sr2, s_sr3, s_pattern, s_last_timeout;
uint32_t g_guard_invalid, g_invalid_mmio;

static uint8_t Pattern(uint32_t address)
{
    uint32_t x = address + 0x9E3779B9U;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    switch (s_pattern)
    {
        case 0U: return 0U;
        case 1U: return 255U;
        case 2U: return 0x55U;
        case 3U: return 0xAAU;
        case 4U: return (uint8_t)address;
        default: return (uint8_t)x;
    }
}

static HAL_StatusTypeDef ReadCommand(QSPI_HandleTypeDef *h, QSPI_CommandTypeDef *c, uint32_t timeout)
{
    uint32_t n = 0U;
    BspQspiStatus_t status;
    BspQspiDiagnostics_t diag;
    (void)h;
    s_calls++; g_commands++;
    CHECK(__get_PRIMASK() == 0U && timeout > 0U && timeout <= 100U);
    CHECK(c->InstructionMode == QSPI_INSTRUCTION_1_LINE);
    CHECK(c->AlternateByteMode == QSPI_ALTERNATE_BYTES_NONE);
    CHECK(c->DdrMode == QSPI_DDR_MODE_DISABLE && c->SIOOMode == QSPI_SIOO_INST_EVERY_CMD);
    if (c->Instruction == 0x05U || c->Instruction == 0x35U || c->Instruction == 0x15U)
        CHECK(c->AddressMode == QSPI_ADDRESS_NONE && c->NbData == 1U &&
            c->DataMode == QSPI_DATA_1_LINE && c->DummyCycles == 0U);
    else
    {
        s_data_commands++;
        CHECK(c->Instruction == 0x03U || c->Instruction == 0x0BU || c->Instruction == 0x6BU);
        CHECK(c->AddressMode == QSPI_ADDRESS_1_LINE && c->AddressSize == QSPI_ADDRESS_24_BITS);
        CHECK(c->Address < BSP_QSPI_CAPACITY_BYTES && c->NbData <= 256U &&
            c->NbData <= BSP_QSPI_CAPACITY_BYTES - c->Address);
        CHECK(c->DataMode == (c->Instruction == 0x6BU ? QSPI_DATA_4_LINES : QSPI_DATA_1_LINE));
        CHECK(c->DummyCycles == (c->Instruction == 0x03U ? 0U : 8U));
        if (c->Instruction == 0x03U)
            CHECK(s_diag.serial_clock_hz <= 50000000U);
    }
    s_cmd = *c;
    QUADSPI->DLR = c->NbData - 1U;
    s_last_timeout = timeout;
    if (s_fault == 15U)
    {
        CHECK(BSP_QSPI_ReadStatus(&status, 1000U) == SYS_ERR_BUSY);
        CHECK(BSP_QSPI_Read(BSP_QSPI_READ_FAST, 0U, s_reference, 1U, &n, 1000U) == SYS_ERR_BUSY);
        CHECK(BSP_QSPI_GetDiagnostics(&diag) == SYS_ERR_BUSY);
        CHECK(BSP_QSPI_GetId(&n) == SYS_ERR_BUSY);
        CHECK(BSP_QSPI_DeInit() == SYS_ERR_BUSY);
        CHECK(BSP_QSPI_Init() == SYS_ERR_BUSY);
    }
    if (s_calls == s_at)
    {
        if (s_fault >= 1U && s_fault <= 3U)
        {
            h->ErrorCode = s_fault == 3U ? HAL_QSPI_ERROR_TIMEOUT : HAL_QSPI_ERROR_TRANSFER;
            return s_fault == 1U ? HAL_BUSY : (s_fault == 2U ? HAL_ERROR : HAL_TIMEOUT);
        }
        if (s_fault == 9U) QUADSPI->DLR++;
        if (s_fault == 10U) QUADSPI->CR ^= QUADSPI_CR_SSHIFT;
    }
    s_tick += s_delay;
    return HAL_OK;
}

static HAL_StatusTypeDef ReadReceive(QSPI_HandleTypeDef *h, uint8_t *data, uint32_t timeout)
{
    uint32_t i;
    CHECK(timeout > 0U && timeout <= s_last_timeout);
    s_reads++;
    h->RxXferSize = s_cmd.NbData;
    h->RxXferCount = 0U;
    for (i = 0U; i < s_cmd.NbData; i++)
        data[i] = s_cmd.Instruction == 0x05U ? (uint8_t)s_sr1 :
            (s_cmd.Instruction == 0x35U ? (uint8_t)s_sr2 :
            (s_cmd.Instruction == 0x15U ? (uint8_t)s_sr3 : Pattern(s_cmd.Address + i)));
    if (s_calls == s_at)
    {
        if (s_fault >= 4U && s_fault <= 6U)
        {
            h->ErrorCode = s_fault == 6U ? HAL_QSPI_ERROR_TIMEOUT : HAL_QSPI_ERROR_TRANSFER;
            return s_fault == 4U ? HAL_BUSY : (s_fault == 5U ? HAL_ERROR : HAL_TIMEOUT);
        }
        if (s_fault == 7U) h->RxXferCount = 1U;
        if (s_fault == 8U) h->RxXferSize++;
        if (s_fault == 11U) h->ErrorCode = HAL_QSPI_ERROR_TRANSFER;
        if (s_fault == 12U) s_tick += 2000U;
        if (s_fault == 13U) TIM6->CR1 = 0U;
        if (s_fault == 14U) QUADSPI->CR |= QUADSPI_CR_DMAEN;
    }
    s_tick += s_delay;
    return HAL_OK;
}

static HAL_StatusTypeDef ReadAbort(QSPI_HandleTypeDef *h)
{
    CHECK(h->Timeout == 100U);
    if (s_fault == 16U) return HAL_ERROR;
    QUADSPI->SR = 0U;
    return HAL_OK;
}

int Q12_TestRun(uint32_t id)
{
    static const uint32_t addresses[] = {0U, 1U, 0xFFU, 0x100U, 0xFFFU, 0x1000U, 0xFFFFFFU};
    static const uint32_t lengths[] = {1U, 2U, 3U, 4U, 255U, 256U, 257U, 512U, 1024U, 4096U};
    uint32_t n = 0xFFFFFFFFU, i, address = 0U, size = 512U, before;
    BspQspiStatus_t status, untouched;
    BspQspiDiagnostics_t diag;
    SysResult_t result;
    BspQspiReadMode_t mode = BSP_QSPI_READ_QUAD_OUTPUT;
    CHECK(Q1_TestRun(0U) == 0);
    s_read_command_hook = ReadCommand;
    s_read_receive_hook = ReadReceive;
    s_read_abort_hook = ReadAbort;
    s_sr2 = 2U;
    s_pattern = 5U;
    s_at = 4U;
    memset(s_output, 0xCC, sizeof(s_output));
    memset(&status, 0xA5, sizeof(status)); untouched = status;
    if (id < 420U)
    {
        s_pattern = id / 70U;
        address = addresses[(id / 10U) % 7U]; size = lengths[id % 10U];
        if (size > BSP_QSPI_CAPACITY_BYTES - address)
        {
            CHECK(BSP_QSPI_Read(mode, address, s_output + 1U, size, &n, 1000U) == SYS_ERR_INVALID_ARG);
            CHECK(s_calls == 0U && s_diag.fault == 0U);
        }
        else
        {
            CHECK(BSP_QSPI_Read(BSP_QSPI_READ_FAST, address, s_reference, size, &n, 1000U) == SYS_OK && n == size);
            CHECK(BSP_QSPI_Read(mode, address, s_output + 1U, size, &n, 1000U) == SYS_OK && n == size);
            CHECK(memcmp(s_reference, s_output + 1U, size) == 0);
            for (i = 0U; i < size; i++) CHECK(s_output[i + 1U] == Pattern(address + i));
            CHECK(s_output[0] == 0xCCU && s_output[size + 1U] == 0xCCU);
            CHECK(s_diag.read_completed == size && s_diag.read_stage == BSP_QSPI_READ_STAGE_COMPLETE);
        }
        return (int)g_failure;
    }
    id -= 420U;
    if (id < 20U)
    {
        before = s_calls;
        g_guard_invalid = 1U;
        switch (id)
        {
            case 0: result = BSP_QSPI_Read(mode, 0U, NULL, 1U, &n, 1000U); break;
            case 1: result = BSP_QSPI_Read(mode, 0U, s_output, 1U, NULL, 1000U); break;
            case 2: size = 0U; goto invalid;
            case 3: mode = (BspQspiReadMode_t)-1; goto invalid;
            case 4: mode = (BspQspiReadMode_t)3; goto invalid;
            case 5: address = 0x1000000U; goto invalid;
            case 6: address = 0xFFFFFFFFU; goto invalid;
            case 7: address = 0xFFFFFFU; size = 2U; goto invalid;
            case 8: size = 0xFFFFFFFFU; goto invalid;
            case 9: result = BSP_QSPI_Read(mode, 0U, s_output, 1U, &n, 0U); break;
            case 10: result = BSP_QSPI_Read(mode, 0U, s_output, 1U, &n, 60001U); break;
            case 11: result = BSP_QSPI_ReadStatus(NULL, 1000U); break;
            case 12: result = BSP_QSPI_ReadStatus(&status, 0U); break;
            case 13: result = BSP_QSPI_ReadStatus(&status, 60001U); break;
            case 14: result = BSP_QSPI_Read(mode, 0U, &n, 4U, &n, 1000U); break;
            case 15: result = BSP_QSPI_Read(mode, 0U, (void *)0xFFFFFFFEU, 4U, &n, 1000U); break;
            case 16: result = BSP_QSPI_Read(mode, 0U, s_output, 4U, (uint32_t *)0xFFFFFFFFU, 1000U); break;
            case 17: address = 0xFFFF00U; size = 257U; goto invalid;
            case 18: address = 0xFFF001U; size = 4096U; goto invalid;
            default: address = 1U; size = 0x1000000U; goto invalid;
        }
        goto invalid_done;
invalid:
        result = BSP_QSPI_Read(mode, address, s_output, size, &n, 1000U);
invalid_done:
        g_guard_invalid = 0U;
        CHECK(g_invalid_mmio == 0U);
        CHECK(result == SYS_ERR_INVALID_ARG && s_calls == before && s_diag.fault == 0U);
        return (int)g_failure;
    }
    id -= 20U;
    if (id < 48U)
    {
        //12种事务异常，分别注入三条状态命令及第一条数据命令。
        s_fault = id % 12U + 1U; s_at = id / 12U + 1U;
        result = BSP_QSPI_Read(mode, 0U, s_output + 1U, size, &n, 1000U);
        CHECK(result != SYS_OK && n == 0U && s_diag.fault == 1U && s_diag.ready == 0U);
        CHECK(s_diag.read_completed == 0U && s_diag.read_result == result);
        CHECK(s_output[1] == 0xCCU);
        CHECK(BSP_QSPI_GetDiagnostics(&diag) == SYS_OK && diag.fault == 1U);
        before = s_calls;
        CHECK(BSP_QSPI_Read(mode, 0U, s_output, 1U, &n, 1000U) == SYS_ERR_INVALID_STATE);
        CHECK(s_calls == before && BSP_QSPI_Init() == SYS_ERR_INVALID_STATE);
        return (int)g_failure;
    }
    id -= 48U;
    switch (id)
    {
        case 0: s_sr2 = 0U; break;
        case 1: s_sr1 = 1U; break;
        case 2: s_sr2 = 0x82U; break;
        case 3: mode = BSP_QSPI_READ_STANDARD; break;
        case 4: s_test_ipsr = 16U; break;
        case 5: __set_PRIMASK(1U); break;
        case 6: __set_BASEPRI(0x40U); break;
        case 7: __set_FAULTMASK(1U); break;
        case 8: TIM6->CR1 = 0U; break;
        case 9: TIM6->DIER = 0U; break;
        case 10: NVIC->ISER[(uint32_t)TIM6_DAC_IRQn >> 5U] = 0U; break;
        case 11: s_busy = true; break;
        case 12: s_diag.ready = 0U; break;
        case 13: s_diag.fault = 1U; break;
        case 14: s_fault = 15U; break;
        case 15: s_tick = 0xFFFFFFF0U; break;
        case 16: s_fault = 12U; s_at = 5U; break;
        case 17: s_fault = 7U; s_at = 5U; break;
        case 18: s_fault = 16U; s_sr2 = 2U; QUADSPI->SR = QUADSPI_SR_BUSY; break;
        case 19: s_fault = 13U; break;
        case 20: s_fault = 14U; break;
        case 21: s_delay = 4U; break;
        case 22: QUADSPI->CCR = QUADSPI_CCR_FMODE; break;
        case 23: QUADSPI->DCR ^= QUADSPI_DCR_FSIZE; break;
        case 24: s_hclk = 48000000U; s_diag.kernel_clock_hz = s_hclk;
            s_diag.prescaler = 0U; s_diag.serial_clock_hz = s_hclk;
            s_qspi.Init.ClockPrescaler = 0U; QUADSPI->CR &= ~QUADSPI_CR_PRESCALER;
            mode = BSP_QSPI_READ_STANDARD; break;
        case 25: s_sr1 = 2U; s_sr3 = 0x60U;
            CHECK(BSP_QSPI_ReadStatus(&status, 1000U) == SYS_OK);
            CHECK(status.wel == 1U && status.busy == 0U && status.qe == 1U && status.sr3 == 0x60U);
            CHECK(s_data_commands == 0U && s_diag.status_valid_mask == 7U); return (int)g_failure;
        case 26: s_fault = 5U; s_at = 3U;
            CHECK(BSP_QSPI_ReadStatus(&status, 1000U) != SYS_OK);
            CHECK(memcmp(&status, &untouched, sizeof(status)) == 0 && s_diag.status_valid_mask == 3U);
            return (int)g_failure;
        case 27: s_hclk--; break;
        default: CHECK(0); return (int)g_failure;
    }
    result = BSP_QSPI_Read(mode, 0U, s_output + 1U, size, &n, id == 21U ? 40U : 1000U);
    if (id <= 13U)
    {
        CHECK(result != SYS_OK && n == 0U && s_data_commands == 0U);
        CHECK(s_diag.fault == (id == 13U ? 1U : 0U));
    }
    else if (id == 14U || id == 15U || id == 24U)
    {
        CHECK(result == SYS_OK && n == size && s_diag.fault == 0U);
        for (i = 0U; i < size; i++) CHECK(s_output[i + 1U] == Pattern(i));
    }
    else
    {
        CHECK(result != SYS_OK && s_diag.fault == 1U);
        if (id == 16U || id == 17U)
        {
            CHECK(n == 256U);
            for (i = 0U; i < n; i++) CHECK(s_output[i + 1U] == Pattern(i));
            for (i = n; i < size; i++) CHECK(s_output[i + 1U] == 0xCCU);
        }
        if (id == 18U) CHECK(s_diag.read_cleanup_result != SYS_OK && s_owned);
        if (id == 19U || id == 20U) CHECK(s_diag.read_cleanup_result == SYS_ERR_INVALID_STATE);
    }
    return (int)g_failure;
}
