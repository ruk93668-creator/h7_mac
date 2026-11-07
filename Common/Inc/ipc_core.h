#ifndef IPC_CORE_H
#define IPC_CORE_H

#include "ipc_shared.h"
#include <stdbool.h>

//端口表仅存于本核普通内存，不写入共享协议
typedef struct
{
    void (*barrier)(void);
    uint32_t (*enter)(void);
    void (*leave)(uint32_t state);
    bool (*is_thread)(void);
} IpcCorePort_t;

//绑定唯一共享对象与静态端口，只供Service初始化使用
SysResult_t IPC_Core_Bind(volatile IpcShared_t *shared, const IpcCorePort_t *port);

#if defined(CORE_CM7)

//CM4在原启动等待期间，由CM7建立协议头和本核初始块
SysResult_t IPC_Core_ColdStart(void);
#endif

//初始化本核状态块，CM4必须先验证CM7已发布的协议头
SysResult_t IPC_Core_InitializeLocal(const IpcState_t *state);

//在初始化窗口内发布本核状态，不允许指定对端目标
SysResult_t IPC_Core_PublishLocal(const IpcState_t *state);

//关闭本阶段的写入窗口，运行期不开放发布上下文
SysResult_t IPC_Core_EndInitialization(void);

//在初始化窗口绑定唯一运行期写者，关闭窗口后不可替换
SysResult_t IPC_Core_ConfigureRuntime(bool (*is_owner)(void *context), void *context);

//只允许预先授权的执行者发布，保留初始化发布接口的关闭边界
SysResult_t IPC_Core_PublishRuntime(const IpcState_t *state);

#if defined(CORE_CM7)

//只允许唯一CM7发布者提交正式V3业务载荷
SysResult_t IPC_Core_PublishRuntimeApplication(const IpcState_t *state,
    const IpcApplicationStatus_t *application);
#endif

//读取本核完整快照，失败不修改输出
SysResult_t IPC_Core_ReadLocal(IpcStatusSnapshot_t *snapshot);

//读取对端完整快照，不把一致性成功当作在线证明
SysResult_t IPC_Core_ReadPeer(IpcStatusSnapshot_t *snapshot);

//读取本地累计诊断，不触碰共享区
SysResult_t IPC_Core_GetDiagnostics(IpcDiagnostics_t *diagnostics);

#endif
