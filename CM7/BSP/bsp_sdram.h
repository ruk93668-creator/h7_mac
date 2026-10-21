#ifndef CM7_BSP_SDRAM_H
#define CM7_BSP_SDRAM_H

#include "sys_result.h"
#include <stdbool.h>

//仅开放Bank2起始64KB，不代表整片可供业务使用
#define BSP_SDRAM_BASE       0xD0000000UL
#define BSP_SDRAM_WINDOW     0x00010000UL
#define BSP_SDRAM_SDCLK_HZ   110000000UL

//初始化FMC、SDRAM时钟和芯片时序
SysResult_t BSP_SDRAM_Init(void);

//查询SDRAM初始化是否已成功完成
bool BSP_SDRAM_IsReady(void);

#endif
