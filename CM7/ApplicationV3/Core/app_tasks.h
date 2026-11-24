#ifndef CM7_APP_TASKS_H
#define CM7_APP_TASKS_H

#include "sys_result.h"
#include <stdint.h>

//应用任务准备与启动屏障的只读诊断
typedef struct
{
    //记录已成功创建的应用任务
    uint32_t created_tasks;
    //表明启动所需资源已准备完成
    uint32_t prepared;
    //标记启动屏障已放行，避免重复放行
    uint32_t boot_released;
    //最近一次操作或终结结果，保留原错误码
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
