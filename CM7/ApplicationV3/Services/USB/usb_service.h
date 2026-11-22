#ifndef CM7_V3_USB_SERVICE_H
#define CM7_V3_USB_SERVICE_H

#include "app_context.h"
#include "sys_result.h"

#if defined(APP_V3_BOARD_SMOKE) && APP_V3_BOARD_SMOKE == 2 && defined(APP_V3_BOARD_STRESS) && APP_V3_BOARD_STRESS != 0
#define APP_V3_USB_FAULT_TRACE_ENABLED 1

typedef struct
{
    uint32_t captured;
    uint32_t time_ms;
    uint32_t configured;
    SysResult_t core_result;
    SysResult_t observe_result;
} AppV3UsbFaultTrace_t;

//仅USB Task写入，测试结束后由Keil Watch读取
extern volatile AppV3UsbFaultTrace_t g_app_v3_usb_first_fault;
#endif

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
