#ifndef CM7_CONTROL_TASK_H
#define CM7_CONTROL_TASK_H

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

    //任务自身的运行阶段
    uint32_t state;

    //最近一次接收的控制命令编号
    uint32_t command_id;

    //保存故障来源，不被后续停止成功覆盖
    SysResult_t last_fault;
} ControlTaskDebug_t;

//提供Control任务的调试状态
extern volatile ControlTaskDebug_t g_control_task_debug;

//创建并登记唯一的静态Control任务
SysResult_t ControlTask_Create(void);

//请求协调停止数据面
SysResult_t ControlTask_RequestStop(void);

//请求协调启动数据面
SysResult_t ControlTask_RequestStart(void);

#endif
