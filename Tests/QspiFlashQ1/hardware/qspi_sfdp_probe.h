#include "qspi_sfdp_diagnostic.h"

#define QSPI_SFDP_ARM_MAGIC 0x53464450UL
#define QSPI_SFDP_SAMPLES 3U
#define QSPI_SAMPLE_BYTES 16U

typedef struct
{
    uint32_t address, bytes, confirmed_03, confirmed_0b, differences, non_ff_03, non_ff_0b;
    SysResult_t result_03, result_0b;
    uint8_t data_03[QSPI_SAMPLE_BYTES], data_0b[QSPI_SAMPLE_BYTES];
} QspiSfdpSample_t;

volatile uint32_t g_qspi_sfdp_arm;
volatile struct
{
    uint32_t phase, complete, signature_ok, samples_attempted;
    SysResult_t sfdp_result, diagnostics_result;
    uint8_t sfdp[QSPI_SFDP_BYTES];
    BspQspiDiagnostics_t sfdp_diagnostics, last_diagnostics;
    QspiSfdpSample_t samples[QSPI_SFDP_SAMPLES];
} g_qspi_sfdp_report;

static void QspiSfdp_Run(void)
{
    static const uint32_t addresses[QSPI_SFDP_SAMPLES] = {0x000000U, 0x000FF8U, 0xFFFFF0U};
    uint8_t sfdp[QSPI_SFDP_BYTES] = {0};
    QspiSfdpSample_t sample;
    BspQspiDiagnostics_t diagnostics;
    uint32_t i, j;
    g_qspi_sfdp_report.phase = 1U;
    g_qspi_sfdp_report.sfdp_result = QspiDiagnostic_ReadSfdp(sfdp);
    if (BSP_QSPI_GetDiagnostics(&diagnostics) == SYS_OK)
        g_qspi_sfdp_report.sfdp_diagnostics = diagnostics;
    for (i = 0U; i < sizeof(sfdp); i++)
        g_qspi_sfdp_report.sfdp[i] = sfdp[i];
    if (g_qspi_sfdp_report.sfdp_result == SYS_OK)
    {
        g_qspi_sfdp_report.signature_ok = sfdp[0] == 0x53U && sfdp[1] == 0x46U &&
            sfdp[2] == 0x44U && sfdp[3] == 0x50U;
        g_qspi_sfdp_report.phase = 2U;
        for (i = 0U; i < QSPI_SFDP_SAMPLES; i++)
        {
            sample = (QspiSfdpSample_t){0};
            sample.address = addresses[i];
            sample.bytes = QSPI_SAMPLE_BYTES;
            sample.result_03 = BSP_QSPI_Read(BSP_QSPI_READ_STANDARD, sample.address,
                sample.data_03, sample.bytes, &sample.confirmed_03, 100U);
            sample.result_0b = SYS_ERR_INVALID_STATE;
            if (sample.result_03 == SYS_OK)
                sample.result_0b = BSP_QSPI_Read(BSP_QSPI_READ_FAST, sample.address,
                    sample.data_0b, sample.bytes, &sample.confirmed_0b, 100U);
            for (j = 0U; j < sample.confirmed_03; j++)
                if (sample.data_03[j] != 0xFFU)
                    sample.non_ff_03++;
            for (j = 0U; j < sample.confirmed_0b; j++)
                if (sample.data_0b[j] != 0xFFU)
                    sample.non_ff_0b++;
            if (sample.result_03 == SYS_OK && sample.result_0b == SYS_OK)
                for (j = 0U; j < sample.bytes; j++)
                    if (sample.data_03[j] != sample.data_0b[j])
                        sample.differences++;
            g_qspi_sfdp_report.samples[i] = sample;
            g_qspi_sfdp_report.samples_attempted++;
            if (sample.result_03 != SYS_OK || sample.result_0b != SYS_OK)
                break;
        }
    }
    g_qspi_sfdp_report.diagnostics_result = BSP_QSPI_GetDiagnostics(&diagnostics);
    if (g_qspi_sfdp_report.diagnostics_result == SYS_OK)
        g_qspi_sfdp_report.last_diagnostics = diagnostics;
    __DMB();
    g_qspi_sfdp_report.phase = 3U;
    g_qspi_sfdp_report.complete = 1U;
}
