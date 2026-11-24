#ifndef QSPI_H2_H
#define QSPI_H2_H

#include "stm32h7xx_hal.h"
#include "bsp_qspi.h"

#define H2_SIGNATURE 0x51344832UL
#define H2_ARM_FAULT 0x48324631UL
#define H2_ARM_RECOVER 0x48325232UL
#define H2_FIRST 0x00FFE000UL
#define H2_BYTES (2U * BSP_QSPI_SECTOR_BYTES)
#define H2_PROBE_BYTES 16U
#define H2_OPS 6U

enum
{
    H2_BOOT, H2_WAIT_FAULT, H2_INIT, H2_NORMAL, H2_INJECT,
    H2_FAULT, H2_WAIT_RECOVER, H2_RECOVER, H2_AFTER, H2_DONE, H2_PHASES
};

typedef struct
{
    uint32_t tick, tim_count, tim_cr1, tim_dier, tim_sr, tim_irq, cpu_masks;
    uint32_t qspi_gate, qspi_mux, gpio_gates, cr, sr, owned;
    uint32_t pins[11];
} H2Resources_t;

typedef struct
{
    uint32_t valid;
    SysResult_t result, diagnostics_result, fault_result;
    H2Resources_t resources;
    BspQspiDiagnostics_t diagnostics;
    BspQspiFaultDiagnostics_t fault;
    uint32_t commands[H2_OPS], received[H2_OPS];
} H2Snapshot_t;

typedef struct
{
    uint32_t signature, version, size, phase, complete;
    SysResult_t result, init, id_result, status_result, fault_read, blocked_init, blocked_read;
    SysResult_t recover, repeat_recover, recovered_id_result;
    uint32_t id, recovered_id, consumed, injected, denied, completed, blocked_completed;
    uint32_t rx_original, rx_returned, rx_error, rx_size, rx_remaining, rx_state;
    uint32_t rx_tick, cleanup_calls, cleanup_hal, cleanup_state, cleanup_cr, cleanup_sr;
    uint32_t recover_entry_tick, recover_entry_tim_count;
    uint32_t restored, before_valid, after_valid, quad_valid;
    uint32_t before_crc[2], after_crc[2], quad_crc[2], mismatch_address;
    BspQspiStatus_t initial_status;
    H2Resources_t initial_resources;
    H2Snapshot_t stage[H2_PHASES];
} H2Report_t;

extern volatile uint32_t g_qspi_h2_arm;
extern volatile H2Report_t g_qspi_h2_report;
extern uint8_t g_qspi_h2_before[H2_BYTES], g_qspi_h2_after[H2_BYTES];

void H2_Boot(void);
void H2_Step(void);
void H2_BootFailure(void);
uint32_t H2_ControllerOwned(void);
HAL_StatusTypeDef H2_Command(QSPI_HandleTypeDef *, QSPI_CommandTypeDef *, uint32_t);
HAL_StatusTypeDef H2_Receive(QSPI_HandleTypeDef *, uint8_t *, uint32_t);
HAL_StatusTypeDef H2_DeInit(QSPI_HandleTypeDef *);

#endif
