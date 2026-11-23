//只由现有 M7 测试任务调用；无新任务、无自动下载、无任何 Flash 写操作。
#define Q12_ARM_MAGIC 0x51325244UL
volatile uint32_t g_q12_hw_arm;
typedef struct
{
    uint32_t version, phase, complete, address, blocks, bytes;
    uint32_t mismatches, non_ff, non_uniform, first_byte;
    uint32_t crc_fast, crc_quad, fast_ms, quad_ms, started_ms, finished_ms;
    uint32_t confirmed, scan_complete, data_integrity_verified;
    SysResult_t result;
    BspQspiDiagnostics_t diagnostics;
} Q12HardwareReport_t;
volatile Q12HardwareReport_t g_q12_hw_report;
static uint8_t s_fast[4096], s_quad[4096];

static uint32_t Q12_Crc(uint32_t crc, const uint8_t *data, uint32_t size)
{
    uint32_t i, bit;
    for (i = 0U; i < size; i++)
    {
        crc ^= data[i];
        for (bit = 0U; bit < 8U; bit++)
            crc = (crc >> 1) ^ ((crc & 1U) != 0U ? 0xEDB88320U : 0U);
    }
    return crc;
}

static SysResult_t Q12_Compare(uint32_t address, uint32_t size)
{
    uint32_t n = 0U, start, i;
    SysResult_t result;
    g_q12_hw_report.address = address;
    start = HAL_GetTick();
    result = BSP_QSPI_Read(BSP_QSPI_READ_FAST, address, s_fast, size, &n, 1000U);
    g_q12_hw_report.fast_ms += HAL_GetTick() - start;
    g_q12_hw_report.confirmed = n;
    if (result != SYS_OK || n != size)
        return result != SYS_OK ? result : SYS_ERR_PROTOCOL;
    start = HAL_GetTick();
    result = BSP_QSPI_Read(BSP_QSPI_READ_QUAD_OUTPUT, address, s_quad, size, &n, 1000U);
    g_q12_hw_report.quad_ms += HAL_GetTick() - start;
    g_q12_hw_report.confirmed = n;
    if (result != SYS_OK || n != size)
        return result != SYS_OK ? result : SYS_ERR_PROTOCOL;
    for (i = 0U; i < size; i++)
    {
        if (s_fast[i] != s_quad[i]) g_q12_hw_report.mismatches++;
        if (s_fast[i] != 0xFFU) g_q12_hw_report.non_ff++;
        if (g_q12_hw_report.bytes == 0U && i == 0U) g_q12_hw_report.first_byte = s_fast[i];
        if (s_fast[i] != g_q12_hw_report.first_byte) g_q12_hw_report.non_uniform++;
    }
    g_q12_hw_report.crc_fast = Q12_Crc(g_q12_hw_report.crc_fast, s_fast, size);
    g_q12_hw_report.crc_quad = Q12_Crc(g_q12_hw_report.crc_quad, s_quad, size);
    g_q12_hw_report.bytes += size;
    g_q12_hw_report.blocks++;
    return g_q12_hw_report.mismatches == 0U ? SYS_OK : SYS_ERR_PROTOCOL;
}

static void Q12_Scan(void)
{
    static const uint32_t addresses[] = {0U, 1U, 0xFFU, 0x100U, 0xFFFU, 0x1000U, 0xFFFFFFU};
    static const uint32_t lengths[] = {1U, 2U, 3U, 4U, 255U, 256U, 257U, 512U, 1024U, 4096U};
    uint32_t address, i, j;
    BspQspiDiagnostics_t diag;
    SysResult_t result = SYS_OK;
    g_q12_hw_report.version = 1U;
    g_q12_hw_report.phase = 1U;
    g_q12_hw_report.started_ms = HAL_GetTick();
    g_q12_hw_report.crc_fast = g_q12_hw_report.crc_quad = 0xFFFFFFFFU;
    for (address = 0U; address < BSP_QSPI_CAPACITY_BYTES && result == SYS_OK; address += 4096U)
    {
        result = Q12_Compare(address, 4096U);
        if (HAL_GetTick() - g_q12_hw_report.started_ms > 120000U && result == SYS_OK)
            result = SYS_ERR_TIMEOUT;
        vTaskDelay(pdMS_TO_TICKS(1U));
    }
    g_q12_hw_report.scan_complete = address == BSP_QSPI_CAPACITY_BYTES && result == SYS_OK;
    g_q12_hw_report.phase = 2U;
    for (i = 0U; i < sizeof(addresses) / sizeof(addresses[0]) && result == SYS_OK; i++)
        for (j = 0U; j < sizeof(lengths) / sizeof(lengths[0]) && result == SYS_OK; j++)
            if (lengths[j] <= BSP_QSPI_CAPACITY_BYTES - addresses[i])
                result = Q12_Compare(addresses[i], lengths[j]);
    g_q12_hw_report.crc_fast ^= 0xFFFFFFFFU;
    g_q12_hw_report.crc_quad ^= 0xFFFFFFFFU;
    g_q12_hw_report.result = result;
    if (BSP_QSPI_GetDiagnostics(&diag) == SYS_OK)
        g_q12_hw_report.diagnostics = diag;
    //一致/非均匀仍不能代替物理 IO0..IO3 波形证据，始终留给人工验收。
    g_q12_hw_report.data_integrity_verified = 0U;
    g_q12_hw_report.finished_ms = HAL_GetTick();
    __DMB();
    g_q12_hw_report.phase = 3U;
    g_q12_hw_report.complete = 1U;
}
