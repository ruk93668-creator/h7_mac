#ifndef CM7_CONTROL_TASK_H
#define CM7_CONTROL_TASK_H

#include "sys_result.h"
#include <stdint.h>

typedef struct
{
    uint32_t boot_seen;
    uint32_t wake_count;
    uint32_t timeout_count;
    uint32_t stack_free_words;
    SysResult_t result;
} ControlTaskDebug_t;

extern volatile ControlTaskDebug_t g_control_task_debug;

//调度器启动前创建唯一静态Control任务
SysResult_t ControlTask_Create(void);

#endif
