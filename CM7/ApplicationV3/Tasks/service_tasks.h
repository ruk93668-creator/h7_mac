#ifndef CM7_V3_SERVICE_TASKS_H
#define CM7_V3_SERVICE_TASKS_H

#include "app_events.h"
#include "../Runtime/service_mailbox.h"

//启动只引用已有静态栈，清单项必须在整个任务生命期有效。
typedef struct
{
    RuntimeEventTarget_t owner;
    const char *name;
    TaskFunction_t entry;
    StackType_t *stack;
    uint32_t stack_words;
    UBaseType_t priority;
    uint32_t boot_events;
} AppV3ThreadSpec_t;

SysResult_t AppV3_CreateThread(const AppV3ThreadSpec_t *thread);
void AppV3_ServiceRun(void *argument, AppV3ServiceMailbox_t *mailbox, uint32_t (*advance)(void));
SysResult_t ControlTask_Create(void);

#endif
