#ifndef CM4_BSP_IIC_H
#define CM4_BSP_IIC_H

#include "sys_result.h"
#include <stdbool.h>

//通过I2C2阻塞发送数据，返回本次发送结果
 SysResult_t BSP_IIC_WriteSync(
    uint8_t addr7,
    uint8_t *data,
    uint16_t len
);
 
//复制数据并启动I2C2异步发送，结果通过TakeResult读取
SysResult_t BSP_IIC_WriteAsync(
    uint8_t addr7,
    const uint8_t *data,
    uint16_t len
);

//轮询异步发送结果并检查超时，有结果时填写result并返回true
bool BSP_IIC_TakeResult(
    SysResult_t *result
);
#endif /* CM4_BSP_IIC_H */
