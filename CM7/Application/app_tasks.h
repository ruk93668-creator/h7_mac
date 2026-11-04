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

//提供任务组织层的调试状态
extern volatile AppTasksDebug_t g_app_tasks_debug;

//统一创建三个业务任务和事件适配
SysResult_t App_TasksCreate(void);

//记录Framework初始化结果并准备启动屏障
SysResult_t App_TasksRecordInitialization(SysResult_t result);

//发布业务任务启动屏障
SysResult_t App_TasksReleaseBoot(void);

#endif
