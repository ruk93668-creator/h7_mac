#include "app_usb.h"
#include "usb_mode.h"
#include "usb_uvc.h"
#include "app_uvc_bridge.h"
#include "platform_time.h"
#include <stddef.h>

//只保存USB业务协调状态，底层模式、端点、Token及租约仍归原模块
typedef struct
{
    bool started;//记录业务START是否成功
    bool stopping;//记录后台停止是否仍需推进
    bool stop_command;//记录STOP是否等待完成结果
    AppMessage_t ack;//保存当前命令对应的回复
    bool ack_pending;//记录ACK是否尚未成功入队
    SysResult_t fault;//保存原业务故障记录
    bool fault_reported;//记录FAULT是否已经成功入队
    SysResult_t last_result;//保存最近业务操作结果
    uint32_t command_id;//保存最近接收的命令编号
    uint32_t fps_tick;//保存EOF统计窗口起点
    uint32_t fps_frames;//保存窗口起始EOF累计值
    uint32_t completed_frames;//保存本轮采样的跨Class累计EOF
    uint32_t measured_fps_milli;//保存按原公式计算的设备侧FPS
    AppUsbDiagnosticObserver_t observer;//保存只做Debug投影的回调
} AppUsbContext_t;

//保存唯一USB业务上下文，Task不保留第二份业务标记
static AppUsbContext_t s_usb;

//在原赋值位置投影指定诊断字段，不决定业务执行
static void App_USBObserve(AppUsbDiagnosticField_t field);

//登记只做诊断投影的静态回调
void App_USBSetDiagnosticObserver(AppUsbDiagnosticObserver_t observer)
{
    s_usb.observer = observer;
}

//保留原ACK和STOP等待期间的命令接收门控
bool App_USBCanAcceptCommand(void)
{
    return !s_usb.ack_pending && !s_usb.stop_command;
}

//处理已经出队的一条命令，保持原START与STOP语义
void App_USBHandleCommand(const AppMessage_t *command)
{
    SysResult_t result;

    //空参数不构造回复，正常路径仍由Task先检查出队门控
    if (command == NULL)
        return;

    //先保存ACK和命令诊断，再执行原命令分支
    s_usb.ack = *command;
    s_usb.ack.type = APP_MESSAGE_ACK;
    s_usb.ack.source = RUNTIME_TARGET_USB;
    s_usb.ack.result = SYS_ERR_INVALID_ARG;
    s_usb.command_id = command->command_id;
    App_USBObserve(APP_USB_DIAG_COMMAND);
    if (command->type == APP_MESSAGE_START)
    {
        //重复START仍返回BUSY，并按原规则记录为业务故障
        result = s_usb.started || s_usb.stopping ? SYS_ERR_BUSY : USB_Mode_Start();
        if (result == SYS_OK)
        {
            s_usb.started = true;
            s_usb.fault = SYS_OK;
            s_usb.fault_reported = false;
            s_usb.fps_tick = Platform_TimeGetMs();
            s_usb.fps_frames = USB_UVC_GetCompletedFrameCount();
        }
        //不重新分类BUSY，也不额外重置已经交付的FAULT
        if (result != SYS_OK)
            s_usb.fault = result;
        s_usb.ack.result = result;
        s_usb.ack_pending = true;
        s_usb.last_result = result;
        App_USBObserve(APP_USB_DIAG_RESULT);
    }
    else if (command->type == APP_MESSAGE_STOP)
    {

        //先关提交入口，后续只推进取消、Return和ACK
        s_usb.stopping = true;
        s_usb.stop_command = true;
    }
    else
        s_usb.ack_pending = true;
}

//读取业务发送允许条件，最终提交仍由UVC和Bridge判断
bool App_USBCanTransfer(void)
{
    return s_usb.started && !s_usb.stopping;
}

//读取停止要求，不清除在途状态或帧所有权
bool App_USBIsStopping(void)
{
    return s_usb.stopping;
}

//只推进原模式流程，错误可在同一轮触发Task后续取消路径
void App_USBProcess(void)
{
    SysResult_t result;

    if (s_usb.started && !s_usb.stopping)
    {
        USB_Mode_Process();
        result = USB_Mode_GetLastResult();
        if (result != SYS_OK)
        {
            s_usb.fault = result;
            s_usb.stopping = true;
            s_usb.last_result = result;
            App_USBObserve(APP_USB_DIAG_RESULT);
        }
    }
}

//在显式UVC和Bridge推进后处理停止结果及业务统计
void App_USBFinishRound(bool uvc_this_round)
{
    UsbUvcStreamStatus_t status;
    SysResult_t result;
    uint32_t now, elapsed, frames, total;

    if (s_usb.stopping)
    {
        result = SYS_ERR_BUSY;
        //先确认Return/ACK结束，再调用原Core停止路径
        if (App_UvcBridgeUsbIsIdle())
            result = USB_Mode_Stop();
        else if (uvc_this_round && USB_UVC_GetStreamStatus(&status) == SYS_OK &&
            status.frame_owned && status.state == USB_UVC_RECOVERY && status.result != SYS_OK)
            result = status.result;

        //Abort失败时仍保留原帧和停止要求，只返回失败ACK，不伪造已停
        if (result == SYS_OK)
        {
            s_usb.started = false;
            s_usb.stopping = false;
        }
        //BUSY继续等待，失败ACK发出后仍保留停止要求供后台重试
        if (s_usb.stop_command && result != SYS_ERR_BUSY)
        {
            s_usb.ack.result = result;
            s_usb.ack_pending = true;
            s_usb.stop_command = false;
            s_usb.last_result = result;
            App_USBObserve(APP_USB_DIAG_RESULT);
        }
        if (result != SYS_OK && result != SYS_ERR_BUSY && s_usb.fault == SYS_OK)
            s_usb.fault = result;
    }
    App_USBObserve(APP_USB_DIAG_STATE);
    //累计EOF跨Class保留，保持原采样和取时顺序
    total = USB_UVC_GetCompletedFrameCount();
    s_usb.completed_frames = total;
    App_USBObserve(APP_USB_DIAG_COMPLETED);
    now = Platform_TimeGetMs();
    elapsed = now - s_usb.fps_tick;
    if (elapsed >= 1000U)
    {
        frames = total - s_usb.fps_frames;
        s_usb.measured_fps_milli = frames <= 4294U ? frames * 1000000U / elapsed : 0U;
        App_USBObserve(APP_USB_DIAG_FPS);
        s_usb.fps_tick = now;
        s_usb.fps_frames = total;
    }
}

//读取待发ACK，读取或发送失败均保留业务记录
bool App_USBGetPendingAck(AppMessage_t *message)
{
    if (message == NULL || !s_usb.ack_pending)
        return false;
    *message = s_usb.ack;
    return true;
}

//仅在Task确认ACK成功入队后清除待发标记
void App_USBConfirmAckEnqueued(void)
{
    s_usb.ack_pending = false;
}

//独立构造FAULT，不让ACK发送失败阻断故障交付
bool App_USBGetPendingFault(AppMessage_t *message)
{
    AppMessage_t fault = {.type = APP_MESSAGE_FAULT};

    if (message == NULL || s_usb.fault == SYS_OK || s_usb.fault_reported)
        return false;
    fault.source = RUNTIME_TARGET_USB;
    fault.result = s_usb.fault;
    *message = fault;
    return true;
}

//仅在Task确认FAULT成功入队后记录已经交付
void App_USBConfirmFaultEnqueued(void)
{
    s_usb.fault_reported = true;
}

//读取当前业务副本，不采样底层或修改业务字段
void App_USBGetStatus(AppUsbRuntimeStatus_t *status)
{
    if (status == NULL)
        return;
    status->state = s_usb.stopping ? 2U : (s_usb.started ? 1U : 0U);
    status->command_id = s_usb.command_id;
    status->last_result = s_usb.last_result;
    status->completed_frames = s_usb.completed_frames;
    status->measured_fps_milli = s_usb.measured_fps_milli;
}

//读取最近业务结果，不覆盖Task独立保存的等待错误
SysResult_t App_USBGetLastResult(void)
{
    return s_usb.last_result;
}

//在原诊断位置生成副本，回调只更新指定的Debug字段
static void App_USBObserve(AppUsbDiagnosticField_t field)
{
    AppUsbRuntimeStatus_t status;

    if (s_usb.observer != NULL)
    {
        App_USBGetStatus(&status);
        s_usb.observer(field, &status);
    }
}
