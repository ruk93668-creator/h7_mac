#ifndef CM7_USB_DEVICE_H
#define CM7_USB_DEVICE_H

#include "sys_result.h"
#include "../BSP/bsp_usb.h"
#include "usbd_def.h"
#include <stdbool.h>

//生命周期取证默认关闭；只读记录不改变协议恢复决策
#ifndef USB_DEVICE_LIFECYCLE_DIAGNOSTICS
#define USB_DEVICE_LIFECYCLE_DIAGNOSTICS 0
#endif

#if USB_DEVICE_LIFECYCLE_DIAGNOSTICS
#define USB_DEVICE_LIFECYCLE_TRACE_COUNT 128U

typedef enum
{
    USB_TRACE_START = 1, USB_TRACE_STOP, USB_TRACE_DEINIT,
    USB_TRACE_RESET_BEGIN, USB_TRACE_RESET_END,
    USB_TRACE_CONNECT, USB_TRACE_DISCONNECT_BEGIN, USB_TRACE_DISCONNECT_END,
    USB_TRACE_SETUP_BEGIN, USB_TRACE_SETUP_END,
    USB_TRACE_EP0_IN, USB_TRACE_EP0_OUT, USB_TRACE_EP0_ERROR,
    USB_TRACE_ADDRESS, USB_TRACE_EP0_OPEN, USB_TRACE_EP0_CLOSE
} UsbDeviceLifecycleEvent_t;

typedef struct
{
    uint32_t sequence;
    uint32_t event;
    uint32_t detail;
    uint32_t control_token;
    uint32_t control_status;
    uint32_t device_state;
    uint32_t ep0_state;
    uint32_t device_address;
    uint32_t ep0_flags;
    SysResult_t result;
} UsbDeviceLifecycleEntry_t;

typedef struct
{
    uint32_t sequence;
    bool exhausted;
    UsbDeviceLifecycleEntry_t entries[USB_DEVICE_LIFECYCLE_TRACE_COUNT];
} UsbDeviceLifecycleDiagnostics_t;
#endif

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


//Platform提交序号与BSP硬件身份在同一次快照中绑定，不把Epoch变化当作退休
typedef struct
{
    BspUsbInIdentity_t transfer;
    uint32_t platform_sequence;
    uint32_t stop_attempt;
} UsbBulkInStopTransaction_t;

SysResult_t USB_Device_GetBulkInTransfer(uint8_t ep, UsbBulkInStopTransaction_t *transaction);
SysResult_t USB_Device_BeginBulkInStop(UsbBulkInStopTransaction_t *transaction,
                                     uint32_t timeout_ms, BspUsbInStopProof_t *proof);
SysResult_t USB_Device_PollBulkInStop(const UsbBulkInStopTransaction_t *transaction,
                                    BspUsbInStopProof_t *proof);
SysResult_t USB_Device_CommitBulkInRetirement(const UsbBulkInStopTransaction_t *transaction,
                                            BspUsbInStopProof_t *proof);

//只读停止事务快照；registered_pending仅对应最近一次Stop实际登记的推进结果
typedef enum
{
    USB_STOP_PROGRESS_NONE = 0, USB_STOP_PROGRESS_PENDING,
    USB_STOP_PROGRESS_COMPLETE, USB_STOP_PROGRESS_FAILED
} UsbStopProgressState_t;

typedef struct
{
    uint32_t transaction_id;
    UsbStopProgressState_t state;
    SysResult_t result;
    bool registered_pending;
    uint32_t endpoint_attempt;
    BspUsbInIdentity_t endpoint_transfer;
} UsbStopProgress_t;

SysResult_t USB_Device_GetStopProgress(UsbStopProgress_t *progress);
SysResult_t USB_Device_ReconcileBulkInStop(const UsbBulkInStopTransaction_t *transaction,
                                         BspUsbInStopProof_t *proof);

//延迟状态只保留当前控制事务身份，不代表任何块设备或DMA所有权
//SUBMITTED仅表示硬件提交成功，COMPLETE必须由EP0 IN完成回调确认
typedef enum
{
    USB_CONTROL_NONE = 0,
    USB_CONTROL_WAITING,
    USB_CONTROL_SUBMITTED,
    USB_CONTROL_COMPLETE,
    USB_CONTROL_CANCELLED,
    USB_CONTROL_FAILED
} UsbControlStatus_t;

//仅允许当前Class的零长度OUT Setup登记；Class随后必须返回USBD_BUSY
SysResult_t USB_Device_DeferControlStatus(const USBD_SetupReqTypedef *request, uint32_t *token);

//任务确认外部资源已安全排空后续接状态阶段，不在ISR中调用
SysResult_t USB_Device_CompleteControlStatus(uint32_t token);

//任务超时或外部排空失败时拒绝当前控制事务，不释放外部IO资源
SysResult_t USB_Device_FailControlStatus(uint32_t token);

//查询最近一次实际完成证据；即使新SETUP取消旧资格，历史事实仍可核对
bool USB_Device_WasControlStatusComplete(uint32_t token);

//按Token读取提交与完成状态；新事务覆盖旧槽后返回INVALID_STATE
SysResult_t USB_Device_GetControlStatus(uint32_t token, UsbControlStatus_t *status);

//显式选择完整OUT契约；只在初始化完成、启动前且无在途OUT时可调用
SysResult_t USB_Device_EnableBulkOutLifecycle(void);
typedef struct
{
    BspUsbOutIdentity_t transfer;
    uint32_t stop_attempt;
} UsbBulkOutStopTransaction_t;
SysResult_t USB_Device_GetBulkOutTransfer(uint8_t ep, UsbBulkOutStopTransaction_t *transaction);
SysResult_t USB_Device_BeginBulkOutStop(UsbBulkOutStopTransaction_t *transaction, uint32_t timeout_ms,
                                     BspUsbOutStopProof_t *proof);
SysResult_t USB_Device_PollBulkOutStop(const UsbBulkOutStopTransaction_t *transaction, BspUsbOutStopProof_t *proof);
SysResult_t USB_Device_CommitBulkOutRetirement(const UsbBulkOutStopTransaction_t *transaction, BspUsbOutStopProof_t *proof);
SysResult_t USB_Device_ReconcileBulkOutStop(const UsbBulkOutStopTransaction_t *transaction, BspUsbOutStopProof_t *proof);

#endif
