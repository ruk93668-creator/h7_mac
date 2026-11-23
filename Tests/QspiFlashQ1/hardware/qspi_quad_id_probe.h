#include "qspi_quad_id.h"

#define QSPI_QUAD_ID_ARM_MAGIC 0x51393452UL
#define QSPI_QUAD_ID_TRIALS 8U

typedef struct
{
    SysResult_t result, diagnostics_result;
    uint8_t id[QSPI_QUAD_ID_BYTES];
    uint32_t signature_ok;
    BspQspiDiagnostics_t diagnostics;
} QspiQuadIdTrial_t;

volatile uint32_t g_qspi_quad_id_arm;
volatile struct
{
    uint32_t phase, complete, attempted, matched, expected_clock_hz;
    QspiQuadIdTrial_t trials[QSPI_QUAD_ID_TRIALS];
} g_qspi_quad_id_report;

static void QspiQuadId_Run(void)
{
    uint32_t i;
    QspiQuadIdTrial_t trial;
    g_qspi_quad_id_report.phase = 1U;
    g_qspi_quad_id_report.expected_clock_hz = QSPI_QUAD_ID_DIAGNOSTIC;
    for (i = 0U; i < QSPI_QUAD_ID_TRIALS; i++)
    {
        trial = (QspiQuadIdTrial_t){0};
        trial.result = QspiDiagnostic_ReadQuadId(trial.id);
        trial.signature_ok = trial.result == SYS_OK && trial.id[0] == 0xEFU && trial.id[1] == 0x17U;
        trial.diagnostics_result = BSP_QSPI_GetDiagnostics(&trial.diagnostics);
        g_qspi_quad_id_report.trials[i] = trial;
        g_qspi_quad_id_report.attempted++;
        g_qspi_quad_id_report.matched += trial.signature_ok;
        if (trial.result != SYS_OK || trial.diagnostics_result != SYS_OK)
            break;
    }
    __DMB();
    g_qspi_quad_id_report.phase = 2U;
    g_qspi_quad_id_report.complete = 1U;
}
