#include "write_test.c"

static uint32_t s_h1_scenario, s_h1_opcode, s_h1_reads;
static const uint8_t s_mock_uid[] = {0x11,0x32,0x12,0x56,0x78,0x9A,0xBC,0xDE};

static HAL_StatusTypeDef H1MockCommand(QSPI_HandleTypeDef *handle, QSPI_CommandTypeDef *cmd,
    uint32_t timeout)
{
    s_h1_opcode = cmd->Instruction;
    if (s_h1_opcode == 0x4BU || s_h1_opcode == 0x5AU)
    {
        CHECK(timeout > 0U && cmd->InstructionMode == QSPI_INSTRUCTION_1_LINE);
        CHECK(cmd->Address == 0U && cmd->AddressMode == QSPI_ADDRESS_1_LINE &&
            cmd->AddressSize == QSPI_ADDRESS_24_BITS && cmd->DummyCycles == 8U);
        CHECK(cmd->DataMode == QSPI_DATA_1_LINE && cmd->NbData == (s_h1_opcode == 0x4BU ? 8U : 16U));
        QUADSPI->DLR = cmd->NbData - 1U;
        return HAL_OK;
    }
    if (s_h1_opcode == 0x03U || s_h1_opcode == 0x6BU)
    {
        QSPI_CommandTypeDef fast = *cmd;
        CHECK(cmd->DataMode == (s_h1_opcode == 0x03U ? QSPI_DATA_1_LINE : QSPI_DATA_4_LINES));
        CHECK(cmd->DummyCycles == (s_h1_opcode == 0x03U ? 0U : 8U));
        fast.Instruction = 0x0BU;
        fast.DummyCycles = 8U;
        fast.DataMode = QSPI_DATA_1_LINE;
        return WriteModelCommand(handle, &fast, timeout);
    }
    if (s_h1_opcode == 0x02U || s_h1_opcode == 0x20U)
        CHECK(cmd->Address >= H1_TARGET && cmd->Address < H1_TARGET + H1_SECTOR);
    return WriteModelCommand(handle, cmd, timeout);
}

static HAL_StatusTypeDef H1MockReceive(QSPI_HandleTypeDef *handle, uint8_t *data, uint32_t timeout)
{
    HAL_StatusTypeDef result;
    if (s_h1_opcode == 0x4BU)
    {
        memcpy(data, s_mock_uid, sizeof(s_mock_uid));
        if (s_h1_scenario == 2U)
            data[0] ^= 1U;
        handle->RxXferSize = sizeof(s_mock_uid);
        handle->RxXferCount = 0U;
        return HAL_OK;
    }
    if (s_h1_opcode == 0x5AU)
    {
        memset(data, 0xFF, 16U);
        if (s_h1_scenario != 2U)
            memcpy(data, "SFDP", 4U);
        handle->RxXferSize = 16U;
        handle->RxXferCount = 0U;
        return HAL_OK;
    }
    result = WriteModelReceive(handle, data, timeout);
    if (s_opcode == 0x0BU)
    {
        s_h1_reads++;
#if defined(QSPI_H1_BACKUP120)
        CHECK(s_h1_opcode != 0x03U);
        if (s_h1_scenario == 1U && s_h1_opcode == 0x6BU)
            data[s_bytes - 1U] ^= 1U;
#elif defined(QSPI_H1_BACKUP)
        if (s_h1_scenario == 1U && s_h1_opcode == 0x0BU)
            data[s_bytes - 1U] ^= 1U;
#else
        if (s_h1_scenario == 11U && s_h1_opcode == 0x6BU)
            data[0] ^= 1U;
#endif
    }
    return result;
}

#if defined(QSPI_H1_WRITE)
static void H1MakeInput(uint32_t id, uint32_t op)
{
    H1Input_t *input = &g_qspi_h1_input;
    uint32_t i;
    input->magic = H1_INPUT_MAGIC;
    input->version = H1_VERSION;
    input->operation = op;
    input->address = H1_TARGET;
    input->bytes = H1_SECTOR;
    memcpy(input->uid, s_mock_uid, sizeof(input->uid));
    memset(input->original, 0xFF, H1_SECTOR);
    if (id == 8U)
        input->original[H1_SECTOR - 1U] = 0x55U;
    if (id == 12U)
        input->original[H1_PATTERN_OFFSET] = 0U;
    for (i = 0U; i < H1_SECTOR; i++)
        input->neighbor[i] = (uint8_t)i;
    memcpy(input->current, input->original, H1_SECTOR);
    if (op == H1_ERASE)
    {
        H1_MakePattern();
        memcpy(input->current + H1_PATTERN_OFFSET, s_h1_pattern, H1_PATTERN_BYTES);
    }
    else if (op == H1_RESTORE)
        memset(input->current, 0xFF, H1_SECTOR);
    memcpy(s_array, input->neighbor, H1_SECTOR);
    memcpy(s_array + H1_SECTOR, input->current, H1_SECTOR);
    if (id == 1U)
        input->address -= H1_SECTOR;
    if (id == 3U)
        s_array[0] ^= 1U;
    if (id == 4U)
        s_array[H1_SECTOR] ^= 1U;
    if (id == 6U)
        input->operation = H1_ERASE;
    input->original_crc = H1_Crc(input->original, H1_SECTOR);
    input->neighbor_crc = H1_Crc(input->neighbor, H1_SECTOR);
    input->current_crc = H1_Crc(input->current, H1_SECTOR);
    if (id == 5U)
        input->current_crc ^= 1U;
}
#endif

int H1_TestRun(uint32_t id)
{
    uint32_t mutations;
    WriteModelInit(0U);
    s_h1_scenario = id;
    CHECK(s_diag.serial_clock_hz == H1_CLOCK_HZ && s_diag.prescaler == 240000000U / H1_CLOCK_HZ - 1U);
    s_read_command_hook = H1MockCommand;
    s_read_receive_hook = H1MockReceive;
#if defined(QSPI_H1_BACKUP) || defined(QSPI_H1_BACKUP120)
    s_array[0] = 0x53U;
    s_array[sizeof(s_array) - 1U] = 0xAAU;
#if defined(QSPI_H1_BACKUP120)
    s_sr2 = id == 3U ? 0U : 2U;
#endif
    CHECK(!QspiH1_ArmValid(0U) && !QspiH1_ArmValid(H1_WRITE_ARM | H1_PROGRAM));
    QspiH1_Run(H1_WRITE_ARM | H1_PROGRAM);
    CHECK(g_qspi_h1_capture.complete == 0U && g_commands == 0U);
    QspiH1_Run(H1_BACKUP_ARM);
    CHECK(g_qspi_h1_capture.complete == 1U && g_qspi_h1_capture.magic == H1_CAPTURE_MAGIC);
    CHECK(g_qspi_h1_capture.version == H1_CAPTURE_VERSION);
    CHECK(g_qspi_h1_capture.result == (id == 3U ? SYS_ERR_NOT_SUPPORTED :
        (id == 0U ? SYS_OK : SYS_ERR_PROTOCOL)));
    if (id != 2U)
        CHECK(g_qspi_h1_capture.first[0] == 0x53U &&
              g_qspi_h1_capture.first[H1_CAPTURE_BYTES - 1U] == 0xAAU);
    CHECK(s_h1_reads == (id == 2U ? 0U : (id == 3U ? 32U : 64U)));
#if defined(QSPI_H1_BACKUP120)
    CHECK((memcmp(g_qspi_h1_capture.sfdp, "SFDP", 4U) == 0) == (id != 2U));
#endif
    CHECK(s_mutations == 0U && s_wren == 0U);
    mutations = g_commands;
    QspiH1_Run(H1_BACKUP_ARM);
    CHECK(g_commands == mutations);
#else
    uint32_t op = id == 7U ? H1_ERASE : ((id == 8U || id == 9U) ? H1_RESTORE : H1_PROGRAM);
    SysResult_t expected;
    s_sr2 = id == 10U ? 0U : 2U;
    H1MakeInput(id, op);
    CHECK(!QspiH1_ArmValid(0U) && !QspiH1_ArmValid(H1_BACKUP_ARM));
    QspiH1_Run(H1_BACKUP_ARM);
    CHECK(g_commands == 0U && g_qspi_h1_report.complete == 0U);
    QspiH1_Run(H1_WRITE_ARM | op);
    expected = id == 0U || id == 7U || id == 8U || id == 9U ? SYS_OK :
        ((id == 1U || id == 6U) ? SYS_ERR_INVALID_ARG :
         (id == 10U ? SYS_ERR_INVALID_STATE : SYS_ERR_PROTOCOL));
    CHECK(g_qspi_h1_report.complete == 1U && g_qspi_h1_report.operation == op);
    CHECK(g_qspi_h1_report.result == expected);
    CHECK(g_qspi_h1_report.pattern_crc == 0x6AFF4F17U);
    if (id == 0U)
    {
        CHECK(s_mutations == 4U && g_qspi_h1_report.write.completed == H1_PATTERN_BYTES);
        CHECK(g_qspi_h1_report.fast_crc == 0x6AFF4F17U && g_qspi_h1_report.quad_crc == 0x6AFF4F17U);
    }
    else if (id == 7U)
        CHECK(s_mutations == 1U && g_qspi_h1_report.write.completed == H1_SECTOR);
    else if (id == 8U)
        CHECK(s_mutations == 16U && s_array[sizeof(s_array) - 1U] == 0x55U);
    else if (id == 11U)
        CHECK(s_mutations == 4U && g_qspi_h1_report.differences_at == H1_TARGET + H1_PATTERN_OFFSET);
    else
        CHECK(s_mutations == 0U && s_wren == 0U);
    mutations = g_commands;
    QspiH1_Run(H1_WRITE_ARM | op);
    CHECK(g_commands == mutations);
#endif
    return (int)g_failure;
}
