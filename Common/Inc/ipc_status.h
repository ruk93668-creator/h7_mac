#ifndef IPC_STATUS_H
#define IPC_STATUS_H

#include "sys_result.h"
#include "ipc_fault.h"
#include <stdint.h>

#define IPC_CORE_NOT_READY 0U
#define IPC_CORE_READY     1U
#define IPC_OWNER_CM7      7U
#define IPC_OWNER_CM4      4U

//本地待发布状态，不包含地址或业务对象
typedef struct
{
    uint32_t ready;

    //本阶段不周期递增，只保留协议字段
    uint32_t heartbeat;
    uint32_t fault_flags;
    int32_t last_result;
} IpcState_t;

//读取成功后返回的本地副本，SYS_OK不代表对端在线
typedef struct
{
    uint32_t sequence;
    uint32_t owner_id;
    uint32_t block_bytes;
    uint32_t ready;
    uint32_t heartbeat;
    uint32_t fault_flags;
    int32_t last_result;
    uint32_t reserved[25];
} IpcStatusSnapshot_t;

//本核诊断，不写入未知共享布局
typedef struct
{
    uint32_t fault_flags;
    SysResult_t last_result;
} IpcDiagnostics_t;

#endif
