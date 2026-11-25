#ifndef CM4_BSP_U7_H
#define CM4_BSP_U7_H

#include <stdbool.h>
#include "sys_result.h"

//U7一次采样得到的原始相位及按键电平
typedef struct
{
    uint8_t phase;     //A位于位1，B位于位0
    bool pressed;      //低电平表示按下
} BspU7Sample_t;

//配置U7输入；LSE占用PC15时返回SYS_ERR_NOT_SUPPORTED
SysResult_t BSP_U7_Init(void);

//读取U7相位及按键；仅在初始化成功后调用
BspU7Sample_t BSP_U7_Read(void);

#endif
