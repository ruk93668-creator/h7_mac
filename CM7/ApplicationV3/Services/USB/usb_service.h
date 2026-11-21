#ifndef CM7_V3_USB_SERVICE_H
#define CM7_V3_USB_SERVICE_H

#include "app_context.h"
#include "sys_result.h"

typedef enum
{
    USB_SERVICE_MOCK_START = 1,
    USB_SERVICE_MOCK_STOP,
    USB_SERVICE_SET_PARAMETER,
    USB_SERVICE_QUERY,
    USB_SERVICE_MOCK_FAIL,
    USB_SERVICE_OP_COUNT,
    USB_SERVICE_START = 16,
    USB_SERVICE_STOP,
    USB_SERVICE_HARDWARE_QUERY,
    USB_SERVICE_RECOVER
} UsbServiceOperation_t;

//由其他登记任务提交模拟命令；SYS_OK只表示成功入队
SysResult_t UsbService_Request(uint32_t command_id, UsbServiceOperation_t operation, uint32_t argument);

//静态Registry使用的启动入口，只有所属任务可以准备或撤回资源
//由所属任务逐轮推进真实操作，返回建议等待毫秒数
uint32_t AppV3_UsbAdvance(void);
void AppV3_UsbRun(void *argument);
SysResult_t AppV3_UsbInit(const AppV3Context_t *context);
void AppV3_UsbDiscard(void);

#endif
