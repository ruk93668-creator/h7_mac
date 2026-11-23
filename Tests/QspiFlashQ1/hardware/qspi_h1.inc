//只由 CM7 BSP 在独立诊断构建中编译。生产公共授权保持为空。
volatile uint32_t g_qspi_h1_arm;
static bool s_h1_used;

static SysResult_t H1_ReadFactory(uint32_t opcode, uint8_t *output, uint32_t bytes)
{
    uint32_t started;
    SysResult_t result;
    if (!Qspi_ContextValid())
        return SYS_ERR_INVALID_STATE;
    if (!Qspi_Take())
        return SYS_ERR_BUSY;
    if (s_diag.ready == 0U || s_diag.fault != 0U || s_diag.serial_clock_hz != H1_CLOCK_HZ)
    {
        Qspi_Give();
        return SYS_ERR_INVALID_STATE;
    }
    started = HAL_GetTick();
    Qspi_ReadBegin(UINT32_MAX, 0U, bytes);
    //4Bh 把 24 个单线零位视为占位。5Ah 使用实际地址 0，两者均再加 8 个空周期。
    result = Qspi_ReadTransfer(opcode, 0U, QSPI_ADDRESS_1_LINE, QSPI_DATA_1_LINE,
        8U, bytes, started, 100U);
    if (result == SYS_OK)
    {
        memcpy(output, s_read_buffer, bytes);
        s_diag.read_completed = bytes;
    }
    return Qspi_ReadFinish(result, started);
}

static SysResult_t H1_ReadUid(uint8_t uid[8])
{
    return H1_ReadFactory(0x4BU, uid, 8U);
}

static bool H1_UidValid(const uint8_t uid[8])
{
    uint32_t i;
    uint8_t combined = 0U, intersection = 0xFFU;
    for (i = 0U; i < 8U; i++)
    {
        combined |= uid[i];
        intersection &= uid[i];
    }
    return combined != 0U && intersection != 0xFFU;
}

static SysResult_t H1_Read(BspQspiReadMode_t mode, uint32_t address, uint8_t *data, uint32_t size)
{
    uint32_t confirmed = 0U;
    SysResult_t result = BSP_QSPI_Read(mode, address, data, size, &confirmed, 2000U);
    return result == SYS_OK && confirmed != size ? SYS_ERR_PROTOCOL : result;
}

#if defined(QSPI_H1_BACKUP) || defined(QSPI_H1_BACKUP120)
typedef struct
{
    uint32_t magic, version, complete, address, bytes, jedec, serial_hz;
    SysResult_t result;
    uint8_t uid[8], first[H1_CAPTURE_BYTES], second[H1_CAPTURE_BYTES];
#if defined(QSPI_H1_BACKUP120)
    uint8_t sfdp[16];
#endif
} H1Capture_t;
H1Capture_t g_qspi_h1_capture;
#if defined(QSPI_H1_BACKUP120)
typedef char H1CaptureLayout[(sizeof(H1Capture_t) == 16440U) ? 1 : -1];
#else
typedef char H1CaptureLayout[(sizeof(H1Capture_t) == 16424U) ? 1 : -1];
#endif

bool QspiH1_ArmValid(uint32_t arm)
{
    return arm == H1_BACKUP_ARM;
}

void QspiH1_Run(uint32_t arm)
{
    H1Capture_t *capture = &g_qspi_h1_capture;
#if defined(QSPI_H1_BACKUP120)
    const BspQspiReadMode_t first = BSP_QSPI_READ_FAST, second = BSP_QSPI_READ_QUAD_OUTPUT;
#else
    const BspQspiReadMode_t first = BSP_QSPI_READ_STANDARD, second = BSP_QSPI_READ_FAST;
#endif
    if (s_h1_used || !QspiH1_ArmValid(arm))
        return;
    s_h1_used = true;
    capture->magic = H1_CAPTURE_MAGIC;
    capture->version = H1_CAPTURE_VERSION;
    capture->address = H1_NEIGHBOR;
    capture->bytes = H1_CAPTURE_BYTES;
    capture->jedec = s_diag.jedec_id;
    capture->serial_hz = s_diag.serial_clock_hz;
    capture->result = H1_ReadUid(capture->uid);
    if (capture->result == SYS_OK && !H1_UidValid(capture->uid))
        capture->result = SYS_ERR_PROTOCOL;
#if defined(QSPI_H1_BACKUP120)
    if (capture->result == SYS_OK)
        capture->result = H1_ReadFactory(0x5AU, capture->sfdp, sizeof(capture->sfdp));
    if (capture->result == SYS_OK && memcmp(capture->sfdp, "SFDP", 4U) != 0)
        capture->result = SYS_ERR_PROTOCOL;
#endif
    if (capture->result == SYS_OK)
        capture->result = H1_Read(first, H1_NEIGHBOR, capture->first, H1_CAPTURE_BYTES);
    if (capture->result == SYS_OK)
        capture->result = H1_Read(second, H1_NEIGHBOR, capture->second, H1_CAPTURE_BYTES);
    if (capture->result == SYS_OK && memcmp(capture->first, capture->second, H1_CAPTURE_BYTES) != 0)
        capture->result = SYS_ERR_PROTOCOL;
    __DMB();
    capture->complete = 1U;
}
#else
typedef struct
{
    uint32_t magic, version, operation, address, bytes, original_crc, neighbor_crc, current_crc;
    uint8_t uid[8], original[H1_SECTOR], neighbor[H1_SECTOR], current[H1_SECTOR];
} H1Input_t;
H1Input_t g_qspi_h1_input;
typedef char H1InputLayout[(sizeof(H1Input_t) == 12328U) ? 1 : -1];

typedef struct
{
    uint32_t operation, stage, complete, differences_at, expected, actual;
    uint32_t pattern_crc, fast_crc, quad_crc, original_crc, neighbor_crc;
    SysResult_t result;
    BspQspiWriteDiagnostics_t write;
    uint8_t uid[8];
} H1Report_t;
H1Report_t g_qspi_h1_report;
static const QspiWritePolicy_t s_h1_policy = {H1_TARGET, H1_SECTOR};
static uint8_t s_h1_check[256], s_h1_pattern[H1_PATTERN_BYTES];

static uint32_t H1_CrcUpdate(uint32_t crc, const uint8_t *data, uint32_t bytes)
{
    uint32_t i;
    while (bytes-- != 0U)
    {
        crc ^= *data++;
        for (i = 0U; i < 8U; i++)
            crc = (crc >> 1U) ^ ((crc & 1U) != 0U ? 0xEDB88320UL : 0U);
    }
    return crc;
}

static uint32_t H1_Crc(const uint8_t *data, uint32_t bytes)
{
    return H1_CrcUpdate(UINT32_MAX, data, bytes) ^ UINT32_MAX;
}

static void H1_MakePattern(void)
{
    static const uint8_t prefix[] = {0x00,0xFF,0x55,0xAA,0x0F,0xF0,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80};
    uint32_t i, random = 0x74701301U;
    memcpy(s_h1_pattern, prefix, sizeof(prefix));
    for (i = sizeof(prefix); i < sizeof(s_h1_pattern); i++)
    {
        random ^= random << 13U;
        random ^= random >> 17U;
        random ^= random << 5U;
        s_h1_pattern[i] = (uint8_t)random;
    }
}

static bool H1_AllFf(const uint8_t *data, uint32_t bytes)
{
    while (bytes-- != 0U)
        if (*data++ != 0xFFU)
            return false;
    return true;
}

static bool H1_StateValid(uint32_t op, const H1Input_t *input)
{
    uint32_t i;
    uint8_t expected;
    for (i = 0U; i < H1_SECTOR; i++)
    {
        expected = op == H1_RESTORE ? 0xFFU : input->original[i];
        if (i >= H1_PATTERN_OFFSET && i - H1_PATTERN_OFFSET < H1_PATTERN_BYTES)
        {
            if (op == H1_PROGRAM && expected != 0xFFU)
                return false;
            if (op == H1_ERASE)
                expected = s_h1_pattern[i - H1_PATTERN_OFFSET];
        }
        if (input->current[i] != expected)
            return false;
    }
    return true;
}

static SysResult_t H1_Check(BspQspiReadMode_t mode, uint32_t address, const uint8_t *expected,
    uint32_t bytes, uint32_t *crc)
{
    uint32_t i, chunk, computed = UINT32_MAX;
    SysResult_t result;
    while (bytes != 0U)
    {
        chunk = bytes > sizeof(s_h1_check) ? sizeof(s_h1_check) : bytes;
        result = H1_Read(mode, address, s_h1_check, chunk);
        if (result != SYS_OK)
            return result;
        for (i = 0U; i < chunk; i++)
        {
            uint8_t value = expected == NULL ? 0xFFU : expected[i];
            if (s_h1_check[i] != value)
            {
                g_qspi_h1_report.differences_at = address + i;
                g_qspi_h1_report.expected = value;
                g_qspi_h1_report.actual = s_h1_check[i];
                return SYS_ERR_PROTOCOL;
            }
        }
        computed = H1_CrcUpdate(computed, s_h1_check, chunk);
        if (expected != NULL)
            expected += chunk;
        address += chunk;
        bytes -= chunk;
    }
    if (crc != NULL)
        *crc = computed ^ UINT32_MAX;
    return SYS_OK;
}

static SysResult_t H1_Execute(uint32_t op)
{
    const H1Input_t *input = &g_qspi_h1_input;
    H1Report_t *report = &g_qspi_h1_report;
    BspQspiStatus_t status;
    SysResult_t result;
    H1_MakePattern();
    report->pattern_crc = H1_Crc(s_h1_pattern, sizeof(s_h1_pattern));
    report->stage = 1U;
    if (input->magic != H1_INPUT_MAGIC || input->version != H1_VERSION ||
        input->operation != op || input->address != H1_TARGET || input->bytes != H1_SECTOR ||
        !H1_UidValid(input->uid) || s_diag.jedec_id != BSP_QSPI_EXPECTED_ID ||
        s_diag.serial_clock_hz != 30000000UL)
        return SYS_ERR_INVALID_ARG;
    report->original_crc = H1_Crc(input->original, H1_SECTOR);
    report->neighbor_crc = H1_Crc(input->neighbor, H1_SECTOR);
    if (input->original_crc != report->original_crc || input->neighbor_crc != report->neighbor_crc ||
        input->current_crc != H1_Crc(input->current, H1_SECTOR) ||
        !H1_AllFf(input->original + H1_PATTERN_OFFSET, H1_PATTERN_BYTES) || !H1_StateValid(op, input))
        return SYS_ERR_PROTOCOL;
    result = H1_ReadUid(report->uid);
    if (result != SYS_OK)
        return result;
    if (memcmp(report->uid, input->uid, sizeof(input->uid)) != 0)
        return SYS_ERR_PROTOCOL;
    report->stage = 2U;
    result = BSP_QSPI_ReadStatus(&status, 100U);
    if (result != SYS_OK)
        return result;
    if (status.busy != 0U || status.wel != 0U || status.sus != 0U ||
        (op == H1_PROGRAM && status.qe == 0U))
        return SYS_ERR_INVALID_STATE;
    result = H1_Check(BSP_QSPI_READ_STANDARD, H1_NEIGHBOR, input->neighbor, H1_SECTOR, NULL);
    if (result == SYS_OK)
        result = H1_Check(BSP_QSPI_READ_FAST, H1_TARGET, input->current, H1_SECTOR, NULL);
    if (result != SYS_OK)
        return result;
    report->stage = 3U;
    if (op == H1_PROGRAM)
        result = Qspi_ProgramWithPolicy(&s_h1_policy, H1_TARGET + H1_PATTERN_OFFSET,
            s_h1_pattern, H1_PATTERN_BYTES, 3000U, &report->write);
    else if (op == H1_ERASE)
        result = Qspi_EraseWithPolicy(&s_h1_policy, H1_TARGET,
            input->current, H1_SECTOR, 3000U, &report->write);
    else if (!H1_AllFf(input->original, H1_SECTOR))
        result = Qspi_ProgramWithPolicy(&s_h1_policy, H1_TARGET,
            input->original, H1_SECTOR, 3000U, &report->write);
    if (result != SYS_OK)
        return result;
    report->stage = 4U;
    if (op == H1_PROGRAM)
    {
        result = H1_Check(BSP_QSPI_READ_FAST, H1_TARGET + H1_PATTERN_OFFSET,
            s_h1_pattern, H1_PATTERN_BYTES, &report->fast_crc);
        if (result == SYS_OK)
            result = H1_Check(BSP_QSPI_READ_QUAD_OUTPUT, H1_TARGET + H1_PATTERN_OFFSET,
                s_h1_pattern, H1_PATTERN_BYTES, &report->quad_crc);
        if (result == SYS_OK)
            result = H1_Check(BSP_QSPI_READ_FAST, H1_TARGET, input->original, H1_PATTERN_OFFSET, NULL);
        if (result == SYS_OK)
            result = H1_Check(BSP_QSPI_READ_FAST, H1_TARGET + H1_PATTERN_OFFSET + H1_PATTERN_BYTES,
                input->original + H1_PATTERN_OFFSET + H1_PATTERN_BYTES,
                H1_SECTOR - H1_PATTERN_OFFSET - H1_PATTERN_BYTES, NULL);
    }
    else
        result = H1_Check(BSP_QSPI_READ_FAST, H1_TARGET, op == H1_RESTORE ? input->original : NULL,
            H1_SECTOR, &report->fast_crc);
    if (result == SYS_OK)
        result = H1_Check(BSP_QSPI_READ_FAST, H1_NEIGHBOR, input->neighbor, H1_SECTOR, NULL);
    return result;
}

bool QspiH1_ArmValid(uint32_t arm)
{
    return arm == (H1_WRITE_ARM | H1_PROGRAM) || arm == (H1_WRITE_ARM | H1_ERASE) ||
        arm == (H1_WRITE_ARM | H1_RESTORE);
}

void QspiH1_Run(uint32_t arm)
{
    uint32_t op;
    if (s_h1_used || !QspiH1_ArmValid(arm))
        return;
    s_h1_used = true;
    op = arm & 0xFFU;
    g_qspi_h1_report.operation = op;
    g_qspi_h1_report.differences_at = UINT32_MAX;
    g_qspi_h1_report.result = H1_Execute(op);
    __DMB();
    g_qspi_h1_report.complete = 1U;
}
#endif
