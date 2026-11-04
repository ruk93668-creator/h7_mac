#ifndef CM7_PLATFORM_RTOS_H
#define CM7_PLATFORM_RTOS_H

#include "sys_result.h"
#include <stdint.h>

typedef struct
{
    uint32_t started;
    uint32_t tick_count;
    uint32_t idle_iterations;
    uint32_t fault_line;
    const char *fault_file;
    uint32_t cfsr;
    uint32_t hfsr;
} PlatformRtosDebug_t;

extern volatile PlatformRtosDebug_t g_rtos_debug;

//启动已创建任务的调度器，成功后不返回，不创建业务任务
SysResult_t Platform_RtosStart(void);

//记录内核断言或栈溢出并停机，供Keil观察
void Platform_RtosAssert(const char *file, uint32_t line);

//HardFault入口保存SCB故障寄存器，不执行恢复
void Platform_RtosRecordFault(void);

#endif
