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

//提供RTOS环境的调试状态
extern volatile PlatformRtosDebug_t g_rtos_debug;

//启动FreeRTOS调度器，不创建业务任务
SysResult_t Platform_RtosStart(void);

//记录断言位置并停机
void Platform_RtosAssert(const char *file, uint32_t line);

//保存处理器故障寄存器
void Platform_RtosRecordFault(void);

#endif
