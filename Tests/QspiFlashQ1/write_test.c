//私有授权窗口只存在于 Unicorn 模型。此翻译单元不属于 Keil 工程。
#include "behavior_test.c"

#define MODEL_BASE 0xFFE000UL
static const QspiWritePolicy_t s_model = {MODEL_BASE, 8192U};
static uint8_t s_array[8192], s_pattern[520];
static uint32_t s_scenario, s_mock_tick_calls, s_opcode, s_address, s_bytes;
static uint32_t s_mutations, s_wren, s_wel, s_busy_polls, s_received, s_full_preflight;
static uint32_t s_sr1, s_sr2, s_sr3, s_abort, s_verified_until;

static uint32_t WriteTick(void)
{
    if ((++s_mock_tick_calls & 7U) == 0U)
        s_tick++;
    return s_tick;
}

static HAL_StatusTypeDef WriteModelCommand(QSPI_HandleTypeDef *handle, QSPI_CommandTypeDef *cmd,
    uint32_t timeout)
{
    uint32_t bit_limit;
    (void)handle;
    g_commands++;
    CHECK(timeout > 0U && timeout <= 100U && __get_PRIMASK() == 0U);
    CHECK(cmd->InstructionMode == QSPI_INSTRUCTION_1_LINE && cmd->DdrMode == QSPI_DDR_MODE_DISABLE);
    CHECK(cmd->AlternateByteMode == QSPI_ALTERNATE_BYTES_NONE && cmd->SIOOMode == QSPI_SIOO_INST_EVERY_CMD);
    s_opcode = cmd->Instruction;
    s_address = cmd->Address;
    s_bytes = cmd->NbData;
    if (s_bytes != 0U)
        QUADSPI->DLR = s_bytes - 1U;
    switch (s_opcode)
    {
        case 0x06U:
            CHECK(cmd->AddressMode == QSPI_ADDRESS_NONE && cmd->DataMode == QSPI_DATA_NONE);
            CHECK(s_busy_polls == 0U && s_wel == 0U);
            if (s_wren == 0U)
                CHECK(s_received >= s_full_preflight);
            s_wren++;
            if (s_scenario == 19U)
                return HAL_ERROR;
            s_wel = s_scenario != 5U && s_scenario != 45U;
            break;
        case 0x20U:
            s_mutations++;
            CHECK(s_wel == 1U && s_address >= MODEL_BASE && s_address < BSP_QSPI_CAPACITY_BYTES);
            CHECK(s_address % BSP_QSPI_SECTOR_BYTES == 0U && s_bytes == 0U);
            CHECK(cmd->AddressMode == QSPI_ADDRESS_1_LINE && cmd->AddressSize == QSPI_ADDRESS_24_BITS);
            CHECK(cmd->DataMode == QSPI_DATA_NONE && cmd->DummyCycles == 0U);
            if (s_scenario == 35U || s_scenario == 36U)
                return HAL_ERROR;
            memset(&s_array[s_address - MODEL_BASE], 0xFF, BSP_QSPI_SECTOR_BYTES);
            if (s_scenario == 37U)
                s_array[s_address - MODEL_BASE + 4095U] = 0U;
            s_busy_polls = 2U;
            break;
        case 0x02U:
            s_mutations++;
            CHECK(s_wel == 1U && s_address >= MODEL_BASE && s_address < BSP_QSPI_CAPACITY_BYTES);
            bit_limit = BSP_QSPI_PAGE_BYTES - (s_address % BSP_QSPI_PAGE_BYTES);
            CHECK(s_bytes != 0U && s_bytes <= bit_limit);
            CHECK(cmd->AddressMode == QSPI_ADDRESS_1_LINE && cmd->AddressSize == QSPI_ADDRESS_24_BITS);
            CHECK(cmd->DataMode == QSPI_DATA_1_LINE && cmd->DummyCycles == 0U);
            if (s_scenario == 6U)
                return HAL_ERROR;
            break;
        case 0x0BU:
            CHECK(s_address >= MODEL_BASE && s_address < BSP_QSPI_CAPACITY_BYTES &&
                s_bytes <= BSP_QSPI_CAPACITY_BYTES - s_address);
            CHECK(cmd->AddressMode == QSPI_ADDRESS_1_LINE && cmd->AddressSize == QSPI_ADDRESS_24_BITS);
            CHECK(cmd->DataMode == QSPI_DATA_1_LINE && cmd->DummyCycles == 8U);
            break;
        case 0x05U:
        case 0x35U:
        case 0x15U:
            CHECK(cmd->AddressMode == QSPI_ADDRESS_NONE && s_bytes == 1U);
            CHECK(cmd->DataMode == QSPI_DATA_1_LINE && cmd->DummyCycles == 0U);
            break;
        default:
            CHECK(false);
            return HAL_ERROR;
    }
    return HAL_OK;
}

static HAL_StatusTypeDef WriteModelReceive(QSPI_HandleTypeDef *handle, uint8_t *data,
    uint32_t timeout)
{
    CHECK(timeout > 0U && timeout <= 100U);
    handle->RxXferSize = s_bytes;
    handle->RxXferCount = 0U;
    if (s_opcode == 0x05U)
    {
        if (s_scenario == 46U && s_mutations != 0U)
            return HAL_ERROR;
        data[0] = (uint8_t)(s_sr1 | (s_wel << 1U));
        if (s_busy_polls != 0U)
        {
            data[0] |= 1U;
            if (s_scenario == 40U && s_busy_polls == 2U)
                s_tick += 399U;
            if (s_scenario != 8U && s_scenario != 34U && --s_busy_polls == 0U &&
                s_scenario != 18U && s_scenario != 43U)
                s_wel = 0U;
        }
    }
    else if (s_opcode == 0x35U)
        data[0] = (uint8_t)s_sr2;
    else if (s_opcode == 0x15U)
        data[0] = (uint8_t)s_sr3;
    else if (s_opcode == 0x0BU)
    {
        memcpy(data, &s_array[s_address - MODEL_BASE], s_bytes);
        if (s_wren == 0U)
            s_received += s_bytes;
        else
        {
            s_verified_until = s_address + s_bytes;
            if (s_scenario == 9U)
                data[s_bytes - 1U] ^= 1U;
            if (s_scenario == 47U)
                QUADSPI->CR ^= QUADSPI_CR_PRESCALER;
        }
        if (s_scenario == 20U && s_wren == 0U)
            s_tick += 3000U;
    }
    return HAL_OK;
}

static HAL_StatusTypeDef WriteModelTransmit(QSPI_HandleTypeDef *handle, uint8_t *data, uint32_t timeout)
{
    uint32_t i;
    CHECK(s_opcode == 0x02U && s_wel == 1U && timeout > 0U && timeout <= 100U);
    if (s_mutations > 1U)
        CHECK(s_verified_until == s_address);
    handle->TxXferSize = s_bytes;
    handle->TxXferCount = 0U;
    for (i = 0U; i < s_bytes; i++)
        s_array[s_address - MODEL_BASE + i] &= data[i];
    s_busy_polls = 2U;
    if (s_scenario == 7U || (s_scenario == 10U && s_mutations == 2U) || s_scenario == 11U)
        return HAL_TIMEOUT;
    if (s_scenario == 17U)
        handle->TxXferCount = 1U;
    if (s_scenario == 22U && s_mutations == 1U)
        s_tick += 80U;
    return HAL_OK;
}

static HAL_StatusTypeDef WriteModelAbort(QSPI_HandleTypeDef *handle)
{
    (void)handle;
    s_abort++;
    return s_scenario == 11U || s_scenario == 36U ? HAL_ERROR : HAL_OK;
}

static void WriteModelInit(uint32_t id)
{
    static const uint8_t prefix[] = {0x00,0xFF,0x55,0xAA,0x0F,0xF0,0x01,0x02,
        0x04,0x08,0x10,0x20,0x40,0x80};
    uint32_t i, random = 0x74701301U;
    s_scenario = id;
    s_hclk = 240000000U;
    TIM6->CR1 = TIM_CR1_CEN;
    TIM6->DIER = TIM_DIER_UIE;
    NVIC_EnableIRQ(TIM6_DAC_IRQn);
    NVIC_SetPriority(TIM6_DAC_IRQn, 4U);
    CHECK(BSP_QSPI_Init() == SYS_OK);
    g_commands = 0U;
    s_tick_hook = WriteTick;
    s_read_command_hook = WriteModelCommand;
    s_read_receive_hook = WriteModelReceive;
    s_read_abort_hook = WriteModelAbort;
    s_write_transmit_hook = WriteModelTransmit;
    memset(s_array, 0xFF, sizeof(s_array));
    memcpy(s_pattern, prefix, sizeof(prefix));
    for (i = sizeof(prefix); i < sizeof(s_pattern); i++)
    {
        random ^= random << 13U;
        random ^= random >> 17U;
        random ^= random << 5U;
        s_pattern[i] = (uint8_t)random;
    }
}

static int Erase_TestRun(uint32_t id)
{
    static uint8_t backup[BSP_QSPI_SECTOR_BYTES];
    BspQspiWriteDiagnostics_t diag;
    QspiWritePolicy_t policy = s_model;
    uint32_t address = MODEL_BASE, bytes = sizeof(backup), budget = 2000U, i;
    SysResult_t result, expected;
    WriteModelInit(id);
    if (id == 39U) address += BSP_QSPI_SECTOR_BYTES;
    for (i = 0U; i < sizeof(backup); i++)
        backup[i] = (uint8_t)(i ^ (i >> 3U));
    memcpy(&s_array[address - MODEL_BASE], backup, sizeof(backup));
    if (id == 28U)
    {
        CHECK(BSP_QSPI_EraseSector(address, backup, bytes, budget, &diag) == SYS_ERR_NOT_SUPPORTED);
        CHECK(g_commands == 0U && diag.completed == 0U && diag.opcode == 0x20U);
        return (int)g_failure;
    }
    if (id == 29U) address++;
    if (id == 30U) policy.bytes = BSP_QSPI_SECTOR_BYTES - 1U;
    if (id == 31U) bytes--;
    if (id == 32U) backup[4095] ^= 1U;
    if (id == 33U) s_sr3 = 0x04U;
    if (id == 38U) budget = 500U;
    if (id == 41U) s_tick = UINT32_MAX - 9U;
    if (id == 42U) s_sr2 = 0x80U;
    if (id == 44U) s_sr1 = 4U;
    s_full_preflight = BSP_QSPI_SECTOR_BYTES;
    result = Qspi_EraseWithPolicy(&policy, address, backup, bytes, budget, &diag);
    CHECK(diag.result == result && diag.opcode == 0x20U);
    switch (id)
    {
        case 29U: case 31U:
            expected = SYS_ERR_INVALID_ARG;
            CHECK(g_commands == 0U);
            break;
        case 30U:
            expected = SYS_ERR_NOT_SUPPORTED;
            CHECK(g_commands == 0U);
            break;
        case 32U:
            expected = SYS_ERR_PROTOCOL;
            CHECK(diag.failed_address == address + 4095U && s_wren == 0U && diag.fault == 0U);
            break;
        case 33U: case 44U:
            expected = SYS_ERR_NOT_SUPPORTED;
            CHECK(s_wren == 0U && diag.fault == 0U);
            break;
        case 34U:
            expected = SYS_ERR_TIMEOUT;
            CHECK(diag.elapsed_ms >= 500U && diag.elapsed_ms < 600U);
            CHECK(diag.flash_completion_unknown == 1U && diag.fault == 1U && diag.completed == 0U);
            break;
        case 35U: case 36U:
            expected = SYS_ERR_IO;
            CHECK(diag.flash_completion_unknown == 1U && diag.fault == 1U && s_abort == 1U);
            CHECK((diag.cleanup_result != SYS_OK) == (id == 36U));
            break;
        case 37U:
            expected = SYS_ERR_PROTOCOL;
            CHECK(diag.completed == 3840U && diag.failed_address == address + 4095U);
            CHECK(diag.fault == 1U && diag.flash_completion_unknown == 0U);
            break;
        case 38U:
            expected = SYS_ERR_TIMEOUT;
            CHECK(g_commands == 0U && diag.fault == 0U);
            break;
        case 42U:
            expected = SYS_ERR_INVALID_STATE;
            CHECK(s_wren == 0U && diag.fault == 0U);
            break;
        case 43U:
            expected = SYS_ERR_INVALID_STATE;
            CHECK(diag.flash_completion_unknown == 1U && diag.fault == 1U);
            break;
        case 45U:
            expected = SYS_ERR_PROTOCOL;
            CHECK(s_wren == 1U && s_mutations == 0U && diag.fault == 0U);
            break;
        case 46U:
            expected = SYS_ERR_IO;
            CHECK(diag.flash_completion_unknown == 1U && diag.fault == 1U &&
                diag.stage == BSP_QSPI_WRITE_BUSY && diag.hal_status == HAL_ERROR);
            break;
        case 47U:
            expected = SYS_ERR_INVALID_STATE;
            CHECK(diag.flash_completion_unknown == 0U && diag.fault == 1U &&
                diag.stage == BSP_QSPI_WRITE_VERIFY && diag.completed == 0U);
            break;
        default:
            expected = SYS_OK;
            CHECK(diag.completed == BSP_QSPI_SECTOR_BYTES && diag.failed_address == UINT32_MAX);
            CHECK(s_received == BSP_QSPI_SECTOR_BYTES && s_wren == 1U && s_mutations == 1U && s_abort == 0U);
            for (i = 0U; i < sizeof(backup); i++)
                CHECK(s_array[address - MODEL_BASE + i] == 0xFFU);
            if (id == 40U)
                CHECK(diag.elapsed_ms >= 400U && diag.elapsed_ms < 500U);
            break;
    }
    CHECK(result == expected);
    return (int)g_failure;
}

int Write_TestRun(uint32_t id)
{
    BspQspiWriteDiagnostics_t diag;
    uint32_t address = MODEL_BASE + 255U, bytes = sizeof(s_pattern), budget = 2000U;
    SysResult_t result, expected;
    if (id >= 28U)
        return Erase_TestRun(id);
    WriteModelInit(id);
    s_diag.read_opcode = 0x5AU;
    if (id == 0U)
    {
        CHECK(BSP_QSPI_Program(address, s_pattern, bytes, budget, &diag) == SYS_ERR_NOT_SUPPORTED);
        CHECK(g_commands == 0U && diag.stage == BSP_QSPI_WRITE_CHECK && diag.completed == 0U);
        return (int)g_failure;
    }
    if (id == 1U) address = BSP_QSPI_CAPACITY_BYTES;
    if (id == 2U) address = BSP_QSPI_CAPACITY_BYTES - 400U;
    if (id == 3U) s_array[600U] = 0x80U;
    if (id == 4U) s_sr1 = 4U;
    if (id == 12U) s_sr2 = 0x40U;
    if (id == 13U) s_sr3 = 0x04U;
    if (id == 14U) s_sr2 = 0x80U;
    if (id == 15U) s_tick = UINT32_MAX - 2U;
    if (id == 16U) { address = BSP_QSPI_CAPACITY_BYTES - 1U; bytes = 1U; }
    if (id == 21U) address = MODEL_BASE - 1U;
    if (id == 22U) budget = 100U;
    if (id == 23U) s_sr1 = 1U;
    if (id == 24U) s_wel = 1U;
    if (id == 25U) { address = MODEL_BASE; bytes = 16U; }
    if (id == 26U) s_busy = true;
    if (id == 27U) s_test_ipsr = 15U;
    s_full_preflight = bytes;
    result = Qspi_ProgramWithPolicy(&s_model, address, s_pattern, bytes, budget, &diag);
    CHECK(diag.result == result && s_diag.read_opcode == 0x5AU);
    switch (id)
    {
        case 1U: case 2U:
            expected = SYS_ERR_INVALID_ARG;
            CHECK(g_commands == 0U);
            break;
        case 21U:
            expected = SYS_ERR_NOT_SUPPORTED;
            CHECK(g_commands == 0U);
            break;
        case 3U:
            expected = SYS_ERR_PROTOCOL;
            CHECK(diag.failed_address == MODEL_BASE + 600U && s_wren == 0U && diag.fault == 0U);
            break;
        case 4U: case 12U: case 13U:
            expected = SYS_ERR_NOT_SUPPORTED;
            CHECK(s_wren == 0U && diag.fault == 0U);
            break;
        case 14U: case 24U: case 27U:
            expected = SYS_ERR_INVALID_STATE;
            CHECK(s_wren == 0U && diag.fault == 0U);
            break;
        case 5U:
            expected = SYS_ERR_PROTOCOL;
            CHECK(s_wren == 1U && s_mutations == 0U && diag.fault == 0U);
            break;
        case 6U:
            expected = SYS_ERR_IO;
            CHECK(diag.flash_completion_unknown == 1U && s_abort == 1U);
            break;
        case 7U: case 8U: case 11U:
            expected = SYS_ERR_TIMEOUT;
            CHECK(diag.flash_completion_unknown == 1U && diag.fault == 1U && s_abort == 1U);
            CHECK((diag.cleanup_result != SYS_OK) == (id == 11U));
            break;
        case 9U:
            expected = SYS_ERR_PROTOCOL;
            CHECK(diag.failed_address == address && diag.fault == 1U && diag.flash_completion_unknown == 0U);
            break;
        case 10U:
            expected = SYS_ERR_TIMEOUT;
            CHECK(diag.completed == 1U && diag.failed_address == address + 1U);
            CHECK(s_mutations == 2U && diag.flash_completion_unknown == 1U);
            break;
        case 17U:
            expected = SYS_ERR_PROTOCOL;
            CHECK(diag.flash_completion_unknown == 1U && diag.fault == 1U);
            break;
        case 18U:
            expected = SYS_ERR_INVALID_STATE;
            CHECK(diag.flash_completion_unknown == 1U && diag.fault == 1U);
            break;
        case 19U:
            expected = SYS_ERR_IO;
            CHECK(diag.flash_completion_unknown == 0U && diag.fault == 1U && s_mutations == 0U);
            break;
        case 20U:
            expected = SYS_ERR_TIMEOUT;
            CHECK(s_wren == 0U && diag.completed == 0U);
            break;
        case 22U:
            expected = SYS_ERR_TIMEOUT;
            CHECK(s_mutations == 1U && diag.completed == 1U && diag.failed_address == address + 1U);
            break;
        case 23U: case 26U:
            expected = SYS_ERR_BUSY;
            CHECK(s_wren == 0U && diag.fault == 0U);
            break;
        default:
            expected = SYS_OK;
            CHECK(diag.completed == bytes && diag.failed_address == UINT32_MAX);
            CHECK(memcmp(&s_array[address - MODEL_BASE], s_pattern, bytes) == 0);
            CHECK(s_wren == (bytes > 1U && address % 256U == 255U ? 4U : 1U));
            CHECK(s_wren == s_mutations && s_abort == 0U);
            break;
    }
    CHECK(result == expected);
    if (diag.fault != 0U)
    {
        CHECK(s_diag.ready == 0U && s_diag.fault == 1U);
        CHECK(Qspi_ProgramWithPolicy(&s_model, MODEL_BASE, s_pattern, 1U, budget, &diag) ==
            SYS_ERR_INVALID_STATE);
    }
    return (int)g_failure;
}
