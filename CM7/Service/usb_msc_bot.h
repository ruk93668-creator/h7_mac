#ifndef CM7_USB_MSC_BOT_H
#define CM7_USB_MSC_BOT_H
#include "usb_msc_backend.h"

//13-case矩阵输出由纯函数生成，端点动作由BOT状态机执行
typedef struct
{
    uint8_t number;
    uint8_t status;
    uint32_t transfer;
    bool stall_in;
    bool stall_out;
} MscBotCase_t;
MscBotCase_t USB_MSC_BotCase(uint32_t host_length, bool host_in, uint32_t device_length, bool device_in);
SysResult_t USB_MSC_BotStart(const MscBackendOps_t *backend);
void USB_MSC_BotStop(void);
void USB_MSC_BotProcess(uint32_t now_ms);
void USB_MSC_BotDataIn(void);
void USB_MSC_BotDataOut(uint32_t length);
void USB_MSC_BotClearHalt(uint8_t endpoint);
SysResult_t USB_MSC_BotReset(uint32_t control_token, uint32_t now_ms);
#endif
