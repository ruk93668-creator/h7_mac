#ifndef CM7_USB_DEVICE_H
#define CM7_USB_DEVICE_H

#include "sys_result.h"
#include "usbd_def.h"
#include <stdbool.h>


//USB任务读取同一采样点的链路事实；短临界区内不执行USB操作
typedef struct
{
    //当前USB协议设备状态
    uint8_t dev_state;
    //挂起前的USB协议设备状态
    uint8_t dev_old_state;
    //底层设备已启动并可能持有外设资源
    bool started;
    //已接管底层资源，退出时必须证明设备停止
    bool stop_required;
    //底层当前或最近一次设备操作结果
    SysResult_t last_result;
} UsbDeviceLinkState_t;

//在短临界区读取链路状态并返回当前是否已配置
bool USB_Device_ReadLinkState(UsbDeviceLinkState_t *state);

//端点资源快照，busy期间缓冲区仍由底层持有
typedef struct
{
    //端点已打开的状态
    bool opened;
    //端点可能仍使用传输缓冲，需要等待退出
    bool busy;
    //端点处于协议停止状态
    bool stalled;
    //标识消息语义或端点传输类型，由所属模块按协议解读
    uint8_t type;
    //端点单包最大长度，单位字节
    uint16_t max_packet;
    //底层当前或最近一次设备操作结果
    SysResult_t last_result;
} UsbDeviceEndpointState_t;

//普通C通知，只提示状态变化，不承诺传输或帧已释放
#define USB_DEVICE_EVENT_STATE 1U
//通知任务重新检查端点传输进度
#define USB_DEVICE_EVENT_IO    2U

//链路通知回调，只提示重新检查实际状态
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
