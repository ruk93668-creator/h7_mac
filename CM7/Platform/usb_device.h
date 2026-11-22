#ifndef CM7_USB_DEVICE_H
#define CM7_USB_DEVICE_H

#include "sys_result.h"
#include "usbd_def.h"
#include <stdbool.h>

#if defined(APP_V3_BOARD_SMOKE) && APP_V3_BOARD_SMOKE == 2 && defined(APP_V3_BOARD_STRESS) && APP_V3_BOARD_STRESS != 0
#define APP_V3_USB_FAULT_TRACE_ENABLED 1
#define USB_DEVICE_TRACE_CAPACITY 8U

typedef enum
{
    USB_DEVICE_TRACE_RESET = 1,
    USB_DEVICE_TRACE_SUSPEND,
    USB_DEVICE_TRACE_RESUME,
    USB_DEVICE_TRACE_CONNECT,
    USB_DEVICE_TRACE_DISCONNECT,
    USB_DEVICE_TRACE_SET_CONFIGURATION
} UsbDeviceTraceKind_t;

typedef struct
{
    uint32_t sequence;
    uint32_t tick_ms;
    uint8_t kind;
    uint8_t dev_state;
    uint8_t dev_old_state;
    uint8_t configuration;
} UsbDeviceTraceEvent_t;

typedef struct
{
    uint8_t dev_state;
    uint8_t dev_old_state;
    bool started;
    bool stop_required;
    SysResult_t last_result;
    uint32_t event_sequence;
    uint32_t event_count;
    UsbDeviceTraceEvent_t events[USB_DEVICE_TRACE_CAPACITY];
} UsbDeviceR2Trace_t;

//读取同一采样点的Core状态和最近事件，返回原Configured判据
bool USB_Device_ReadR2Trace(UsbDeviceR2Trace_t *trace);
#endif

//USB任务读取同一采样点的链路事实；短临界区内不执行USB操作
typedef struct
{
    uint8_t dev_state;
    uint8_t dev_old_state;
    bool started;
    bool stop_required;
    SysResult_t last_result;
} UsbDeviceLinkState_t;

bool USB_Device_ReadLinkState(UsbDeviceLinkState_t *state);

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

//绑定USB状态通知接收端
SysResult_t USB_Device_SetEventCallback(UsbDeviceEventCallback_t callback, void *context);

//生命周期由初始化流程或UsbTask串行调用，操作未结束时重入返回BUSY

//初始化通用USB Device环境
SysResult_t USB_Device_Init(void);

//登记外部Class和描述符
SysResult_t USB_Device_RegisterClass(USBD_ClassTypeDef *driver, USBD_DescriptorsTypeDef *descriptors);

//返回当前已登记的配置描述符
uint8_t *USB_Device_GetConfigDescriptor(uint16_t *length);

//启动已登记的USB设备
SysResult_t USB_Device_Start(void);

//停止USB并清理Class资源
SysResult_t USB_Device_Stop(void);

//查询USB设备是否已完成配置
bool USB_Device_IsConfigured(void);

//查询设备级运行结果
SysResult_t USB_Device_GetLastResult(void);

//查询最近一次接口操作结果
SysResult_t USB_Device_GetLastOperationResult(void);

//打开Class数据端点
SysResult_t USB_Device_OpenEp(uint8_t ep, uint8_t type, uint16_t size);

//取消Class数据端点的当前传输
SysResult_t USB_Device_AbortEp(uint8_t ep);

//关闭Class数据端点
SysResult_t USB_Device_CloseEp(uint8_t ep);

//清空空闲Class数据端点的FIFO
SysResult_t USB_Device_FlushEp(uint8_t ep);

//向Class IN端点提交发送
SysResult_t USB_Device_Transmit(uint8_t ep, uint8_t *data, uint32_t size);

//向Class OUT端点提交接收
SysResult_t USB_Device_Receive(uint8_t ep, uint8_t *data, uint32_t size);

//设置Class数据端点STALL
SysResult_t USB_Device_StallEp(uint8_t ep);

//清除Class数据端点STALL
SysResult_t USB_Device_ClearStallEp(uint8_t ep);

//取得端点状态快照
SysResult_t USB_Device_GetEpState(uint8_t ep, UsbDeviceEndpointState_t *state);

//查询Class OUT端点接收长度
uint32_t USB_Device_GetRxSize(uint8_t ep);

//查询去重后的SOF序号
uint32_t USB_Device_GetSofSequence(void);

//查询指定IN端点的提交序号
uint32_t USB_Device_GetInSequence(uint8_t ep);

//查询指定IN端点的完成序号
uint32_t USB_Device_GetInCompleteSequence(uint8_t ep);

//保存并屏蔽USB中断
uint32_t USB_Device_Lock(void);

//恢复USB中断使能状态
void USB_Device_Unlock(uint32_t saved);

#endif
