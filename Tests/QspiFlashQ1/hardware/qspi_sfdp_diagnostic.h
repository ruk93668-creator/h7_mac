#ifndef QSPI_SFDP_DIAGNOSTIC_H
#define QSPI_SFDP_DIAGNOSTIC_H

#include "bsp_qspi.h"

#define QSPI_SFDP_BYTES 16U

//试验构建专用，只读参数表。成功前不修改调用者缓冲区。
SysResult_t QspiDiagnostic_ReadSfdp(uint8_t data[QSPI_SFDP_BYTES]);

#endif
