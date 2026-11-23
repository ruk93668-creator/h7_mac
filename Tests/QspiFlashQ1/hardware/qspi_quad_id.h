#ifndef QSPI_QUAD_ID_H
#define QSPI_QUAD_ID_H

#include "bsp_qspi.h"

#if QSPI_QUAD_ID_DIAGNOSTIC != 30000000 && QSPI_QUAD_ID_DIAGNOSTIC != 120000000
#error Quad ID diagnostic requires a fixed 30 or 120 MHz build
#endif

#define QSPI_QUAD_ID_BYTES 2U

//事务成功才发布两字节原始 ID，签名由测试入口独立判定。
SysResult_t QspiDiagnostic_ReadQuadId(uint8_t id[QSPI_QUAD_ID_BYTES]);

#endif
