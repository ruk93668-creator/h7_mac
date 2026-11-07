#ifndef CM7_IPC_TASK_H
#define CM7_IPC_TASK_H

#include "sys_result.h"
#include <stdint.h>

//IPC任务的本地运行诊断，不写入跨核共享协议
typedef struct
{
    uint32_t boot_seen;//是否收到BOOT屏障
    uint32_t wake_count;//实际推进运行期处理的次数
    uint32_t stack_free_words;//历史最小剩余栈，单位Word
    SysResult_t result;//最近一次本核运行期调用结果，不代表对端ONLINE
    SysResult_t snapshot_result;//最近一次业务汇总或设置结果
} IpcTaskDebug_t;

//保存IPC Task的BOOT、运行次数和栈余量
extern volatile IpcTaskDebug_t g_ipc_task_debug;

//创建唯一静态IPC任务并登记BOOT接收端
SysResult_t IpcTask_Create(void);

#endif
