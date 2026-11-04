#ifndef CM7_USB_DEVICE_H
#define CM7_USB_DEVICE_H

#include "sys_result.h"
#include "usbd_def.h"
#include <stdbool.h>

//端点资源快照，busy期间缓冲区仍由底层持有
typedef struct
{
    bool opened;
    bool busy;
    bool stalled;
    uint8_t type;
    uint16_t max_packet;
    SysResult_t last_result;
} UsbDeviceEndpointState_t;

//普通C通知，只提示状态变化，不承诺传输或帧已释放
#define USB_DEVICE_EVENT_STATE 1U
#define USB_DEVICE_EVENT_IO    2U

typedef void (*UsbDeviceEventCallback_t)(uint32_t events, void *context);

//启动前绑定静态通知接收端，回调可能在USB IRQ中执行
SysResult_t USB_Device_SetEventCallback(UsbDeviceEventCallback_t callback, void *context);

//生命周期接口只允许主循环调用，操作未结束时重入返回BUSY

//初始化Core和BSP，不绑定具体描述符或Class
SysResult_t USB_Device_Init(void);

//停止状态下登记外部Class和静态描述符，注册后才能Start
SysResult_t USB_Device_RegisterClass(USBD_ClassTypeDef *driver, USBD_DescriptorsTypeDef *descriptors);

//返回注册时校验通过的配置，调用者不得修改或释放
uint8_t *USB_Device_GetConfigDescriptor(uint16_t *length);

//启动已经注册的设备，不允许重复启动
SysResult_t USB_Device_Start(void);

//停止硬件并释放端点，失败后必须重试Stop
SysResult_t USB_Device_Stop(void);

//查询当前连接是否已配置
bool USB_Device_IsConfigured(void);

//查询设备级致命错误；普通BUSY、参数错误和ISO丢包不会污染此状态
SysResult_t USB_Device_GetLastResult(void);

//查询最近一次USB API操作结果，不作为设备健康状态
SysResult_t USB_Device_GetLastOperationResult(void);

//申请非EP0端点，能力限制由BSP决定
SysResult_t USB_Device_OpenEp(uint8_t ep, uint8_t type, uint16_t size);

//中止非EP0端点当前传输但保留端点配置
SysResult_t USB_Device_AbortEp(uint8_t ep);

//关闭非EP0端点并取消资源占用，可重复关闭
SysResult_t USB_Device_CloseEp(uint8_t ep);

//清空空闲端点FIFO，传输期间先Close再重新Open
SysResult_t USB_Device_FlushEp(uint8_t ep);

//提交IN传输，完成前禁止重复提交或修改缓冲区
SysResult_t USB_Device_Transmit(uint8_t ep, uint8_t *data, uint32_t size);

//提交OUT接收，完成前禁止重复提交或修改缓冲区
SysResult_t USB_Device_Receive(uint8_t ep, uint8_t *data, uint32_t size);

//设置非EP0端点STALL，是否支持由BSP决定
SysResult_t USB_Device_StallEp(uint8_t ep);

//清除非EP0端点STALL
SysResult_t USB_Device_ClearStallEp(uint8_t ep);

//取得端点状态快照，包括Core管理的EP0；等时未完成只记录端点结果
SysResult_t USB_Device_GetEpState(uint8_t ep, UsbDeviceEndpointState_t *state);

//读取最近一次OUT接收长度
uint32_t USB_Device_GetRxSize(uint8_t ep);

//单调SOF序号，同一硬件帧号不重复计数
uint32_t USB_Device_GetSofSequence(void);

//IN传输提交及真实完成序号，跨连接保持单调
uint32_t USB_Device_GetInSequence(uint8_t ep);
uint32_t USB_Device_GetInCompleteSequence(uint8_t ep);

//只屏蔽USB IRQ的嵌套临界区，保留其他外设中断
uint32_t USB_Device_Lock(void);
void USB_Device_Unlock(uint32_t saved);

#endif
