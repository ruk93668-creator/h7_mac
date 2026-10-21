#ifndef CM7_BSP_DCMI_H
#define CM7_BSP_DCMI_H

#include "sys_result.h"

//唤醒CM4前预配置引脚，避免GPIOB初始化冲突
void BSP_DCMI_EarlyInit(void);

//仅初始化DCMI和DMA，不启动采集
SysResult_t BSP_DCMI_Init(void);

#endif
