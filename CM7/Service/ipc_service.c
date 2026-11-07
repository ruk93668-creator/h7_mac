#include "ipc_service.h"
#include "ipc_platform.h"
#include "ipc_core.h"
#include "framework_init.h"

//记录本核Service是否已经完成一次初始化
static bool s_service_initialized;

//初始化本核IPC状态并关闭05B写入窗口
static SysResult_t IPC_Service_Init(void);

//建立冷启动协议，不改写CM4状态块
SysResult_t IPC_Service_EarlyInit(void)
{
    SysResult_t result;

    //本入口仍在CM4原STOP等待与HSEM0唤醒之间执行
    result = IPC_Core_Bind(&g_ipc, IPC_Platform_GetPort());
    if (result != SYS_OK)
        return result;
    return IPC_Core_ColdStart();
}

//取得本核完整状态副本
SysResult_t IPC_Service_ReadLocal(IpcStatusSnapshot_t *snapshot)
{
    return IPC_Core_ReadLocal(snapshot);
}

//取得对端完整状态副本
SysResult_t IPC_Service_ReadPeer(IpcStatusSnapshot_t *snapshot)
{
    return IPC_Core_ReadPeer(snapshot);
}

//取得本地协议诊断
SysResult_t IPC_Service_GetDiagnostics(IpcDiagnostics_t *diagnostics)
{
    return IPC_Core_GetDiagnostics(diagnostics);
}

//在原SERVICE初始化等级准备本核状态
FRAMEWORK_INIT_REGISTER(IPC_Service_Init, INIT_LEVEL_SERVICE);

//初始化本核IPC状态并关闭05B写入窗口
static SysResult_t IPC_Service_Init(void)
{
    const IpcState_t ready = {IPC_CORE_READY, 0U, 0U, SYS_OK};
    SysResult_t result;

    //Ready只表示本核IPC初始化完成，不表示Application业务已运行
    if (s_service_initialized)
        return SYS_ERR_ALREADY_EXISTS;

    //CM7已在EarlyInit建立本核块，这里只提交Ready
    result = IPC_Core_PublishLocal(&ready);
    if (result != SYS_OK)
        return result;

    //本轮不绑定运行期写者，关闭后仅允许一致性读取
    result = IPC_Core_EndInitialization();
    if (result != SYS_OK)
        return result;
    s_service_initialized = true;
    return SYS_OK;
}
