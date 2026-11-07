#ifndef CM4_IPC_SERVICE_H
#define CM4_IPC_SERVICE_H

#include "ipc_status.h"
#include "ipc_runtime.h"

//取得本核一致快照，不代表业务初始化全部成功
SysResult_t IPC_Service_ReadLocal(IpcStatusSnapshot_t *snapshot);

//取得对端一致快照，不代表对端在线或数据新鲜
SysResult_t IPC_Service_ReadPeer(IpcStatusSnapshot_t *snapshot);

//取得本地协议诊断，未知布局错误只保存在这里
SysResult_t IPC_Service_GetDiagnostics(IpcDiagnostics_t *diagnostics);

//读取本核运行期诊断快照，不写共享区
SysResult_t IPC_Service_GetRuntimeStatus(IpcRuntimeStatus_t *status);

//Watch观察本地诊断，多个字段不保证调试器同时刷新
extern volatile IpcRuntimeStatus_t g_ipc_runtime_debug;

//取得CM7业务完整本地副本，并返回有效性、新鲜度和通信状态
SysResult_t IPC_Service_GetApplicationStatus(IpcApplicationView_t *view);

//Watch观察最近一次Timer产生的业务视图
extern volatile IpcApplicationView_t g_ipc_application_debug;

#endif
