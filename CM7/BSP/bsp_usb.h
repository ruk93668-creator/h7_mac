#ifndef CM7_BSP_USB_H
#define CM7_BSP_USB_H
#include "sys_result.h"
#include <stdint.h>
#include <stdbool.h>

//EP0控制端点最大包长
#define BSP_USB_EP0_MAX_PACKET_SIZE 64U

//USB2控制器包含EP0，共9组双向端点
#define BSP_USB_ENDPOINT_COUNT 9U

//每个非零端点最多保留三种传输类型的规划需求
#define BSP_USB_MAX_ENDPOINT_CONFIGS 48U

typedef struct
{
    uint8_t address;      //方向位加端点编号
    uint8_t type;         //USB类型：1等时、2批量、3中断
    uint16_t max_packet;  //描述符声明的最大包长
} BspUsbEndpointConfig_t;

//停止状态一次规划整个Class所需FIFO，不打开端点
SysResult_t BSP_USB_ConfigureEndpoints(const BspUsbEndpointConfig_t *config, uint32_t count);

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

    //等时端点未完成，由Platform转交当前Class
    void (*iso_incomplete_in)(uint8_t ep);

    //OUT方向同样只上报端点编号
    void (*iso_incomplete_out)(uint8_t ep);

    //普通IN传输分包续传失败
    void (*data_in_error)(uint8_t ep, SysResult_t result);

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

//按FS规范校验地址、类型和包长，IN端点需满足已规划FIFO容量
SysResult_t BSP_USB_OpenEp(uint8_t ep, uint8_t type, uint16_t size);

//取消指定端点的传输，成功后缓冲区不再被硬件访问
SysResult_t BSP_USB_AbortEp(uint8_t ep);

//停止所有非零端点传输，统一清理共享RX FIFO
SysResult_t BSP_USB_AbortAll(void);

//关闭指定方向的端点
SysResult_t BSP_USB_CloseEp(uint8_t ep);

//清空指定方向的端点的FIFO
SysResult_t BSP_USB_FlushEp(uint8_t ep);

//设置指定方向的STALL，等时端点不支持
SysResult_t BSP_USB_StallEp(uint8_t ep);

//清除指定方向的STALL，等时端点不支持
SysResult_t BSP_USB_ClearStallEp(uint8_t ep);

//读取端点STALL状态
bool BSP_USB_IsStallEp(uint8_t ep);

//更新USB设备地址
SysResult_t BSP_USB_SetAddress(uint8_t address);

//发送IN数据；等时每次不超过最大包长，Bulk和Interrupt支持多包

//EP0保留0x00和0x80两种调用地址；缓冲区保持有效直到完成回调
SysResult_t BSP_USB_Transmit(uint8_t ep, uint8_t *data, uint32_t size);

//准备OUT接收，非零端点缓冲区容量须为最大包长整数倍
SysResult_t BSP_USB_Receive(uint8_t ep, uint8_t *data, uint32_t size);

//取得指定OUT端点实际接收字节数
uint32_t BSP_USB_GetRxSize(uint8_t ep);
#endif
