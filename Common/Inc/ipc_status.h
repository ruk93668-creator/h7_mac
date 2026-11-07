#ifndef IPC_STATUS_H
#define IPC_STATUS_H

#include "sys_result.h"
#include "ipc_fault.h"
#include "ipc_application.h"
#include <stdint.h>

#define IPC_CORE_NOT_READY 0U
#define IPC_CORE_READY     1U
#define IPC_OWNER_CM7      7U
#define IPC_OWNER_CM4      4U

//本地待发布状态，不包含地址或业务对象
typedef struct
{
    uint32_t ready;//IPC准备标记，不能直接作为ONLINE证明

    //由唯一运行期发布者推进，成功提交后保留本次计数
    uint32_t heartbeat;//本核成功发布的无符号心跳计数
    uint32_t fault_flags;//累计故障位，可按位组合
    int32_t last_result;//最近操作结果，成功为零、失败为负
} IpcState_t;

//读取成功后返回的本地副本，SYS_OK不代表对端在线
typedef struct
{
    uint32_t sequence;//已通过首尾一致性校验的提交序号
    uint32_t owner_id;//状态块所属核心编号
    uint32_t block_bytes;//状态块长度，用于协议校验
    uint32_t ready;//IPC准备标记，不能直接作为ONLINE证明
    uint32_t heartbeat;//本核成功发布的无符号心跳计数
    uint32_t fault_flags;//累计故障位，可按位组合
    int32_t last_result;//最近操作结果，成功为零、失败为负
    IpcApplicationStatus_t application;
    uint32_t reserved[5];
} IpcStatusSnapshot_t;

//本核诊断，不写入未知共享布局
typedef struct
{
    uint32_t fault_flags;//累计故障位，可按位组合
    SysResult_t last_result;//最近操作结果，成功为零、失败为负
} IpcDiagnostics_t;

#endif
