#ifndef CM7_APP_EVENTS_H
#define CM7_APP_EVENTS_H

#include "runtime_event.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdbool.h>

#define APP_EVENTS_WAIT_FOREVER UINT32_MAX

//准备事件适配和USB状态通知
SysResult_t App_EventsInit(void);

//登记指定目标的任务句柄
SysResult_t App_EventsAttach(RuntimeEventTarget_t target, TaskHandle_t task);

//记录初始化结果并准备启动条件
SysResult_t App_EventsRecordInitialization(SysResult_t result);

//向全部业务任务发布启动屏障
SysResult_t App_EventsReleaseBoot(void);

//阻塞等待当前任务的事件提示
SysResult_t App_EventsWait(RuntimeEventTarget_t target, uint32_t timeout_ms, uint32_t *events);

//检查当前执行者是否为目标任务
bool App_EventsIsOwner(RuntimeEventTarget_t target);

//Control在测试终结时读取现有静态任务的最小剩余栈，单位为StackType_t
bool App_EventsReadStackWatermarks(UBaseType_t remaining_words[RUNTIME_TARGET_COUNT]);

#endif
