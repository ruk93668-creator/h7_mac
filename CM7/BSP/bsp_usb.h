#ifndef CM7_BSP_USB_H
#define CM7_BSP_USB_H
#include "sys_result.h"
#include <stdint.h>
#include <stdbool.h>

//EP0控制端点最大包长
#define BSP_USB_EP0_MAX_PACKET_SIZE 64U

//EP1 IN等时端点最大包长，包含上层协议头
#define BSP_USB_EP1_MAX_PACKET_SIZE 512U

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

//支持EP0双向CTRL 64字节、EP1 IN ISOC 512字节，调用时才打开
SysResult_t BSP_USB_OpenEp(uint8_t ep, uint8_t type, uint16_t size);

//关闭EP0或EP1 IN端点
SysResult_t BSP_USB_CloseEp(uint8_t ep);

//清空EP0或EP1 IN的FIFO
SysResult_t BSP_USB_FlushEp(uint8_t ep);

//设置EP0端点STALL，EP1等时端点不支持
SysResult_t BSP_USB_StallEp(uint8_t ep);

//清除EP0端点STALL，EP1等时端点不支持
SysResult_t BSP_USB_ClearStallEp(uint8_t ep);

//读取端点STALL状态
bool BSP_USB_IsStallEp(uint8_t ep);

//更新USB设备地址
SysResult_t BSP_USB_SetAddress(uint8_t address);

//发送EP0或EP1 IN，EP1每次最多512字节，不使用USB DMA

//EP0保留0x00和0x80两种调用地址；缓冲区保持有效直到完成回调
SysResult_t BSP_USB_Transmit(uint8_t ep, uint8_t *data, uint32_t size);

//仅准备EP0 OUT接收
SysResult_t BSP_USB_Receive(uint8_t ep, uint8_t *data, uint32_t size);

//仅获取EP0 OUT实际接收字节数
uint32_t BSP_USB_GetRxSize(uint8_t ep);
#endif
