#ifndef CM7_APP_TASKS_H
#define CM7_APP_TASKS_H

#include "sys_result.h"
#include <stdint.h>

typedef struct
{
    uint32_t created_tasks;
    uint32_t prepared;
    uint32_t boot_released;
    SysResult_t result;
} AppTasksDebug_t;

extern volatile AppTasksDebug_t g_app_tasks_debug;

//main在Initcall前统一创建三个静态任务，失败后不启动调度器
SysResult_t App_TasksCreate(void);

//main记录Initcall结果，只有成功才允许进入BOOT_READY
SysResult_t App_TasksRecordInitialization(SysResult_t result);

//ControlTask发布启动屏障，不执行Initcall
SysResult_t App_TasksReleaseBoot(void);

#endif
