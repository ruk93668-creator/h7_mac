#ifndef CM4_IPC_SERVICE_H
#define CM4_IPC_SERVICE_H

#include "ipc_status.h"

//取得本核一致快照，不代表业务初始化全部成功
SysResult_t IPC_Service_ReadLocal(IpcStatusSnapshot_t *snapshot);

//取得对端一致快照，不代表对端在线或数据新鲜
SysResult_t IPC_Service_ReadPeer(IpcStatusSnapshot_t *snapshot);

//取得本地协议诊断，未知布局错误只保存在这里
SysResult_t IPC_Service_GetDiagnostics(IpcDiagnostics_t *diagnostics);

#endif
