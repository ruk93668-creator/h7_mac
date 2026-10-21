#ifndef CM7_BSP_CAMERA_H
#define CM7_BSP_CAMERA_H

#include "sys_result.h"

//在唤醒CM4前预配置摄像头引脚
void BSP_Camera_EarlyInit(void);

//初始化XCLK和传感器复位时序
SysResult_t BSP_Camera_Init(void);

//通过软件SCCB读取一个寄存器
SysResult_t BSP_Camera_ReadReg(
    uint16_t reg,
    uint8_t *value
);

//通过软件SCCB写入一个寄存器
SysResult_t BSP_Camera_WriteReg(
	uint16_t reg,
	uint8_t value
);

#endif
