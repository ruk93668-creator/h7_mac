#ifndef CM7_BSP_USB_H
#define CM7_BSP_USB_H
#include "sys_result.h"
#include <stdint.h>
#include <stdbool.h>

//PCD事件由Platform接收，BSP不依赖USB Device Core
typedef struct
{
    void (*setup)(uint8_t *data);
    void (*data_out)(uint8_t ep, uint8_t *data);
    void (*data_in)(uint8_t ep, uint8_t *data);
    void (*reset)(void);
    void (*suspend)(void);
    void (*resume)(void);
    void (*connect)(void);
    void (*disconnect)(void);
} BspUsbCallbacks_t;

//初始化USB2 FS内部PHY、时钟和PCD，不连接主机
SysResult_t BSP_USB_Init(void);
//绑定静态事件表，只允许在USB停止时调用
SysResult_t BSP_USB_SetCallbacks(const BspUsbCallbacks_t *callbacks);
//连接主机并使能USB中断
SysResult_t BSP_USB_Start(void);
//断开主机并禁用USB中断，可再次启动
SysResult_t BSP_USB_Stop(void);
//释放PCD和USB引脚，不关闭其他模块使用的GPIO时钟
SysResult_t BSP_USB_DeInit(void);
//本阶段仅配置EP0，后续Class端点需扩展FIFO分配
SysResult_t BSP_USB_OpenEp(uint8_t ep, uint8_t type, uint16_t size);
//关闭端点
SysResult_t BSP_USB_CloseEp(uint8_t ep);
//清空端点FIFO
SysResult_t BSP_USB_FlushEp(uint8_t ep);
//设置端点STALL
SysResult_t BSP_USB_StallEp(uint8_t ep);
//清除端点STALL
SysResult_t BSP_USB_ClearStallEp(uint8_t ep);
//读取端点STALL状态
bool BSP_USB_IsStallEp(uint8_t ep);
//更新USB设备地址
SysResult_t BSP_USB_SetAddress(uint8_t address);
//启动端点发送，缓冲区由CPU访问，不使用USB DMA
SysResult_t BSP_USB_Transmit(uint8_t ep, uint8_t *data, uint32_t size);
//准备端点接收
SysResult_t BSP_USB_Receive(uint8_t ep, uint8_t *data, uint32_t size);
//读取端点实际接收字节数
uint32_t BSP_USB_GetRxSize(uint8_t ep);
#endif
