#ifndef V2_RTOS_RUNTIME_H
#define V2_RTOS_RUNTIME_H

#include "runtime_event.h"

// 在原任务循环中调用，仅推进隔离的 P8.2 控制事务。
void V2Runtime_Round(RuntimeEventTarget_t task, uint32_t events, uint32_t now);

typedef struct
{
    uint32_t ready;
    uint32_t final;
    int32_t first_error;
    int32_t recovery_error;
    uint32_t execution_state;
    uint32_t confirmed[3];
} V2RuntimeDebug;

extern volatile V2RuntimeDebug g_v2_runtime_debug;
#endif
