#ifndef CM7_IPC_SERVICE_H
#define CM7_IPC_SERVICE_H

#include "ipc_status.h"
#include "ipc_runtime.h"

//在原HSEM0唤醒前建立V2头与CM7本核初始状态
SysResult_t IPC_Service_EarlyInit(void);

//取得本核一致快照，不代表业务初始化全部成功
SysResult_t IPC_Service_ReadLocal(IpcStatusSnapshot_t *snapshot);

//取得对端一致快照，不代表对端在线或数据新鲜
SysResult_t IPC_Service_ReadPeer(IpcStatusSnapshot_t *snapshot);

//取得本地协议诊断，未知布局错误只保存在这里
SysResult_t IPC_Service_GetDiagnostics(IpcDiagnostics_t *diagnostics);

//在调度器启动前登记IPC Task的身份检查，运行期不可替换
SysResult_t IPC_Service_SetRuntimeOwner(bool (*is_owner)(void *context), void *context);

//由IPC Task在BOOT放行后启动运行期计时
SysResult_t IPC_Service_RuntimeStart(uint32_t now);

//只允许IPC Task设置待发布的正式业务载荷
SysResult_t IPC_Service_SetApplication(const IpcApplicationStatus_t *application, uint32_t sampled_tick);

//只允许IPC Task按本地时间推进心跳
SysResult_t IPC_Service_RuntimeProcess(uint32_t now);

//读取本核运行期诊断快照，不写共享区
SysResult_t IPC_Service_GetRuntimeStatus(IpcRuntimeStatus_t *status);

//Watch观察本地诊断，多个字段不保证调试器同时刷新
extern volatile IpcRuntimeStatus_t g_ipc_runtime_debug;

#endif
