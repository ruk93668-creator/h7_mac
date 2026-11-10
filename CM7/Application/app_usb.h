#ifndef CM7_APP_USB_H
#define CM7_APP_USB_H

#include "app_messages.h"
#include <stdbool.h>
#include <stdint.h>

//保存业务状态副本，不携带Core、Class、端点或帧租约对象
typedef struct
{
    uint32_t state;//按停止优先规则生成0、1、2状态
    uint32_t command_id;//最近接收的控制命令编号
    SysResult_t last_result;//最近业务结果，不包含任务等待异常
    uint32_t completed_frames;//本轮采样的跨Class累计EOF数
    uint32_t measured_fps_milli;//按原窗口计算的设备侧FPS
} AppUsbRuntimeStatus_t;

//标记本次只投影哪一个诊断字段，不参与业务决策
typedef enum
{
    APP_USB_DIAG_COMMAND = 0,
    APP_USB_DIAG_RESULT,
    APP_USB_DIAG_STATE,
    APP_USB_DIAG_COMPLETED,
    APP_USB_DIAG_FPS
} AppUsbDiagnosticField_t;

//回调只读取本次副本，禁止保存指针、推进业务或操作硬件
typedef void (*AppUsbDiagnosticObserver_t)(AppUsbDiagnosticField_t field,
    const AppUsbRuntimeStatus_t *status);

//以下运行期接口仅由UsbTask串行调用，不允许ISR或其他任务并发推进
//登记静态诊断回调，不改变业务状态
void App_USBSetDiagnosticObserver(AppUsbDiagnosticObserver_t observer);

//判断ACK和STOP门控是否允许出队下一条控制命令
bool App_USBCanAcceptCommand(void);

//处理一条已出队命令，不直接收发RTOS消息
void App_USBHandleCommand(const AppMessage_t *command);

//返回业务允许条件，不代表Configured、Commit或Alt1已经就绪
bool App_USBCanTransfer(void);

//读取停止要求，供Task显式安排取消和第二次UVC推进
bool App_USBIsStopping(void);

//只推进原USB_Mode流程及错误识别，不隐藏UVC或Bridge数据面
void App_USBProcess(void);

//数据面推进后处理停止及FPS，参数为本轮原位置读取的UVC模式判断
void App_USBFinishRound(bool uvc_this_round);

//复制待发ACK，不消费原记录
bool App_USBGetPendingAck(AppMessage_t *message);

//仅在ACK成功进入Control队列后确认，不代表Control已经处理
void App_USBConfirmAckEnqueued(void);

//独立复制待发FAULT，不受ACK发送失败影响
bool App_USBGetPendingFault(AppMessage_t *message);

//仅在FAULT成功进入Control队列后确认
void App_USBConfirmFaultEnqueued(void);

//只复制业务副本，不读取时钟或推进底层状态
void App_USBGetStatus(AppUsbRuntimeStatus_t *status);

//只读取最近业务操作结果，不覆盖Task等待异常
SysResult_t App_USBGetLastResult(void);

#endif
