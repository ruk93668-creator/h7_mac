#ifndef CM7_USB_TASK_H
#define CM7_USB_TASK_H

#include "sys_result.h"
#include <stdint.h>

typedef struct
{
    uint32_t boot_seen;
    uint32_t wake_count;
    uint32_t timeout_count;
    uint32_t stack_free_words;
    SysResult_t result;
} UsbTaskDebug_t;

extern volatile UsbTaskDebug_t g_usb_task_debug;

//调度器启动前创建唯一静态Usb任务
SysResult_t UsbTask_Create(void);

#endif
