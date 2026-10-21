#ifndef CM7_BSP_SDRAM_H
#define CM7_BSP_SDRAM_H

#include "sys_result.h"
#include <stdbool.h>

//仅开放Bank2起始64KB，不代表整片可供业务使用
#define BSP_SDRAM_BASE       0xD0000000UL
#define BSP_SDRAM_WINDOW     0x00010000UL
#define BSP_SDRAM_SDCLK_HZ   110000000UL

//供调试器查看初始化结果和实际计算频率
extern volatile SysResult_t g_sdram_init_result;
extern volatile uint32_t g_sdram_sdclk_hz;

SysResult_t BSP_SDRAM_Init(void);
bool BSP_SDRAM_IsReady(void);

#endif
