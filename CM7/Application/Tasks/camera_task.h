#ifndef CM7_CAMERA_TASK_H
#define CM7_CAMERA_TASK_H

#include "sys_result.h"
#include <stdint.h>

typedef struct
{

    //是否已经收到启动屏障
    uint32_t boot_seen;

    //累计处理通知的次数
    uint32_t wake_count;

    //累计等待超时次数，永久等待的任务通常保持0
    uint32_t timeout_count;

    //历史最小剩余栈空间，单位为StackType_t
    uint32_t stack_free_words;

    //最近一次任务操作结果
    SysResult_t result;
} CameraTaskDebug_t;

//提供Camera任务的调试状态
extern volatile CameraTaskDebug_t g_camera_task_debug;

//创建并登记唯一的静态Camera任务
SysResult_t CameraTask_Create(void);

#endif
