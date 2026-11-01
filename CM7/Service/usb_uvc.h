#ifndef CM7_USB_UVC_H
#define CM7_USB_UVC_H

#include "sys_result.h"



//注册UVC Class
SysResult_t USB_UVC_Register(void);

#include <stdbool.h>

typedef enum
{
    USB_UVC_WAIT_FRAME = 0,
    USB_UVC_WAIT_SLOT,
    USB_UVC_IN_FLIGHT,
    USB_UVC_WAIT_CONFIRM,
    USB_UVC_CANCELLING,
    USB_UVC_RECOVERY
} UsbUvcStreamState_t;

typedef struct
{
    UsbUvcStreamState_t state;
    SysResult_t result;
    uint32_t frame_offset;
    uint32_t token;
    uint32_t completed_frames;
    bool frame_owned;
} UsbUvcStreamStatus_t;

//主循环处理构包、完成确认及错误恢复，不在IRQ构包
void USB_UVC_Process(void);

//借用一帧外部图像，生命周期由状态查询确认，不生成假帧
SysResult_t USB_UVC_SubmitFrame(const uint8_t *frame, uint32_t length);

//请求主循环取消，底层归还前保留外部图像
SysResult_t USB_UVC_CancelFrame(void);

//查询所有权及最近调度结果
SysResult_t USB_UVC_GetStreamStatus(UsbUvcStreamStatus_t *status);

#endif
