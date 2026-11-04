#ifndef CM7_CAMERA_TASK_H
#define CM7_CAMERA_TASK_H

#include "sys_result.h"
#include <stdint.h>

typedef struct
{
    uint32_t boot_seen;
    uint32_t wake_count;
    uint32_t timeout_count;
    uint32_t stack_free_words;
    SysResult_t result;
} CameraTaskDebug_t;

extern volatile CameraTaskDebug_t g_camera_task_debug;

//调度器启动前创建唯一静态Camera任务
SysResult_t CameraTask_Create(void);

#endif
