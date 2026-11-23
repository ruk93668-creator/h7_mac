#ifndef QSPI_H1_H
#define QSPI_H1_H

#include "bsp_qspi.h"
#include <stdbool.h>

#if !defined(CORE_CM7) || (defined(QSPI_H1_BACKUP) && defined(QSPI_H1_WRITE)) || \
    defined(QSPI_SFDP_DIAGNOSTIC) || defined(QSPI_QUAD_ID_DIAGNOSTIC)
#error Select one H1 diagnostic profile on CM7 only
#endif

#define H1_TARGET 0x00FFF000UL
#define H1_NEIGHBOR 0x00FFE000UL
#define H1_SECTOR BSP_QSPI_SECTOR_BYTES
#define H1_CAPTURE_BYTES (2U * H1_SECTOR)
#define H1_CAPTURE_MAGIC 0x31424351UL
#define H1_INPUT_MAGIC 0x31495751UL
#define H1_VERSION 1U
#define H1_BACKUP_ARM 0x48314252UL
#define H1_WRITE_ARM 0x48315700UL
#define H1_PROGRAM 1U
#define H1_ERASE 2U
#define H1_RESTORE 3U
#define H1_PATTERN_OFFSET 255U
#define H1_PATTERN_BYTES 520U

extern volatile uint32_t g_qspi_h1_arm;
bool QspiH1_ArmValid(uint32_t arm);
void QspiH1_Run(uint32_t arm);

#endif
