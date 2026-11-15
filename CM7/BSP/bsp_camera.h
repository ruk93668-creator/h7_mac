#ifndef CM7_BSP_CAMERA_H
#define CM7_BSP_CAMERA_H

#include "sys_result.h"

//在唤醒CM4前预配置摄像头引脚
void BSP_Camera_EarlyInit(void);

//使能U10数字电源，传感器初始化仍由BSP_Camera_Init完成
void BSP_Camera_PowerEnable(void);

//关闭U10数字电源；仅供停止采集并完成资源释放后的整机断电流程使用
//普通Device_Stop不调用此接口；再次使用必须完整重新初始化和配置传感器
void BSP_Camera_PowerDisable(void);

//初始化数字电源、XCLK和传感器复位时序
SysResult_t BSP_Camera_Init(void);

//通过I2C1读取一个寄存器，仅供启动初始化或Camera所有者任务调用
SysResult_t BSP_Camera_ReadReg(
    uint16_t reg,
    uint8_t *value
);

//通过I2C1写入一个寄存器，仅供启动初始化或Camera所有者任务调用
SysResult_t BSP_Camera_WriteReg(
	uint16_t reg,
	uint8_t value
);

#endif
