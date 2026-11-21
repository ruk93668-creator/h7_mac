#ifndef H7_TINYUSB_EXPERIMENT_H
#define H7_TINYUSB_EXPERIMENT_H
#include <stdbool.h>
#include <stdint.h>
bool TinyUSB_ExperimentPrepare(void);
void TinyUSB_ExperimentRun(void);
typedef struct {
    uint32_t cpu_hz, dwt_first, dwt_second, irq_priority, basepri;
    uint32_t initialized, polls, failures, command, stopped;
} TinyUSBExperimentDebug_t;
extern volatile TinyUSBExperimentDebug_t g_tinyusb_experiment;
#endif
