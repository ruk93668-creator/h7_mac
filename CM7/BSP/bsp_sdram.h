#ifndef CM7_BSP_SDRAM_H
#define CM7_BSP_SDRAM_H

#include "sys_result.h"
#include <stdbool.h>

//V0芯片64MiB，FMC Bank2映射基址
#define BSP_SDRAM_BASE       0xD0000000UL
#define BSP_SDRAM_CAPACITY   0x04000000UL

//业务窗口8MiB，覆盖首1KiB保留区、七缓冲及独立JPEG区
//必须与CM7 MPU Region2一致，新增高地址仍需实板验证
#define BSP_SDRAM_WINDOW     0x00800000UL
#define BSP_SDRAM_END        (BSP_SDRAM_BASE + BSP_SDRAM_WINDOW)

#if BSP_SDRAM_WINDOW == 0UL || \
    (BSP_SDRAM_WINDOW & (BSP_SDRAM_WINDOW - 1UL)) != 0UL || \
    BSP_SDRAM_WINDOW > BSP_SDRAM_CAPACITY || \
    (BSP_SDRAM_BASE & (BSP_SDRAM_WINDOW - 1UL)) != 0UL
#error Invalid SDRAM business window
#endif

#define BSP_SDRAM_SDCLK_HZ   110000000UL

//初始化FMC、SDRAM时钟和芯片时序
SysResult_t BSP_SDRAM_Init(void);

//查询SDRAM初始化是否已成功完成
bool BSP_SDRAM_IsReady(void);

#endif
