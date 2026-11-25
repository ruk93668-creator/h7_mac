#ifndef CM7_V3_SERVICE_TASKS_H
#define CM7_V3_SERVICE_TASKS_H

#include "app_events.h"
#include "../Runtime/service_mailbox.h"

//启动只引用已有静态栈，清单项必须在整个任务生命期有效。
typedef struct
{
    //唯一拥有操作权限的任务身份
    RuntimeEventTarget_t owner;
    //内核任务名称，供调度诊断识别
    const char *name;
    //任务实际运行入口
    TaskFunction_t entry;
    //任务独占的静态栈地址
    StackType_t *stack;
    //静态栈大小，单位为内核字
    uint32_t stack_words;
    //FreeRTOS任务优先级
    UBaseType_t priority;
    //允许运行前必须收到的启动屏障位
    uint32_t boot_events;
} AppV3ThreadSpec_t;

//调度前按静态资源创建并登记唯一任务
SysResult_t AppV3_CreateThread(const AppV3ThreadSpec_t *thread);
//服务线程的共用启动与业务循环入口
void AppV3_ServiceRun(void *argument, AppV3ServiceMailbox_t *mailbox, uint32_t (*advance)(void));
//以固定优先级和静态栈创建ControlTask
SysResult_t ControlTask_Create(void);

#endif
