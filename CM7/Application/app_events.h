#ifndef CM7_APP_EVENTS_H
#define CM7_APP_EVENTS_H

#include "runtime_event.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdbool.h>

#define APP_EVENTS_WAIT_FOREVER UINT32_MAX

//调度器启动前准备事件后端及USB普通C通知
SysResult_t App_EventsInit(void);

//由各任务创建入口登记自己的静态任务句柄
SysResult_t App_EventsAttach(RuntimeEventTarget_t target, TaskHandle_t task);

//记录main中的Initcall结果，失败后禁止发布BOOT_READY
SysResult_t App_EventsRecordInitialization(SysResult_t result);

//仅ControlTask在调度器运行后发布一次启动屏障
SysResult_t App_EventsReleaseBoot(void);

//仅对应任务等待自己的通知，超时单位ms
SysResult_t App_EventsWait(RuntimeEventTarget_t target, uint32_t timeout_ms, uint32_t *events);

#endif
