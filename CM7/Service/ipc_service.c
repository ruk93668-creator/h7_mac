#include "ipc_service.h"
#include "ipc_platform.h"
#include "ipc_core.h"
#include "framework_init.h"
#include <stddef.h>

//记录本核Service是否已经完成一次初始化
static bool s_service_initialized;

//初始化本核IPC状态并关闭05B写入窗口
static SysResult_t IPC_Service_Init(void);

//保存本核唯一运行期上下文，不与其他任务共享写权限
static IpcRuntime_t s_runtime;

//保存静态身份回调，初始化结束后不允许替换
static bool (*s_runtime_owner)(void *context);
static void *s_runtime_context;

//提供Keil观察的运行期诊断副本
volatile IpcRuntimeStatus_t g_ipc_runtime_debug;

//检查调用方是否为本核预先绑定的运行期执行者
static bool IpcService_IsOwner(void *context);

//在短临界区提交诊断副本，与读取接口配对保护
static void IpcService_UpdateDebug(void);


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

    //只绑定预先登记的唯一写者，初始化发布接口仍永久关闭
    if (s_runtime_owner != NULL)
    {
        result = IPC_Core_ConfigureRuntime(IpcService_IsOwner, NULL);
        if (result != SYS_OK)
            return result;
    }

    result = IPC_Core_EndInitialization();
    if (result != SYS_OK)
        return result;
    s_service_initialized = true;
    return SYS_OK;
}

//取得本地诊断的完整副本
SysResult_t IPC_Service_GetRuntimeStatus(IpcRuntimeStatus_t *status)
{
    const IpcCorePort_t *port = IPC_Platform_GetPort();
    uint32_t mask;

    //普通线程内短暂屏蔽调度，避免读取半更新本地诊断
    if (status == NULL || ((uintptr_t)status & 3U) != 0U ||
        (uintptr_t)status > UINTPTR_MAX - (sizeof(*status) - 1U))
        return SYS_ERR_INVALID_ARG;
    if ((uintptr_t)status <= IPC_SHARED_BASE + IPC_SHARED_WINDOW_BYTES - 1U &&
        IPC_SHARED_BASE <= (uintptr_t)status + sizeof(*status) - 1U)
        return SYS_ERR_INVALID_ARG;
    if (port == NULL || !port->is_thread())
        return SYS_ERR_INVALID_STATE;
    mask = port->enter();
    *status = g_ipc_runtime_debug;
    port->leave(mask);
    return SYS_OK;
}

//登记Application提供的静态任务身份检查
SysResult_t IPC_Service_SetRuntimeOwner(bool (*is_owner)(void *context), void *context)
{
    const IpcCorePort_t *port = IPC_Platform_GetPort();

    if (is_owner == NULL)
        return SYS_ERR_INVALID_ARG;
    if (context != NULL && (uintptr_t)context >= IPC_SHARED_BASE &&
        (uintptr_t)context < IPC_SHARED_BASE + IPC_SHARED_WINDOW_BYTES)
        return SYS_ERR_INVALID_ARG;
    if (port == NULL || !port->is_thread())
        return SYS_ERR_INVALID_STATE;
    if (s_service_initialized || s_runtime_owner != NULL)
        return SYS_ERR_INVALID_STATE;
    s_runtime_owner = is_owner;
    s_runtime_context = context;
    return SYS_OK;
}

//在BOOT放行后建立本核运行期基线
SysResult_t IPC_Service_RuntimeStart(uint32_t now)
{
    if (!s_service_initialized || !IpcService_IsOwner(NULL))
        return SYS_ERR_INVALID_STATE;
    return IPC_Runtime_Start(&s_runtime, now);
}

//由唯一IPC Task推进一次运行期处理
SysResult_t IPC_Service_RuntimeProcess(uint32_t now)
{
    SysResult_t result;

    if (!s_service_initialized || !IpcService_IsOwner(NULL))
        return SYS_ERR_INVALID_STATE;
    result = IPC_Runtime_Process(&s_runtime, now);
    IpcService_UpdateDebug();
    return result;
}

//将普通C授权检查转交Application，Service不依赖任务句柄
static bool IpcService_IsOwner(void *context)
{
    (void)context;
    return s_runtime_owner != NULL && s_runtime_owner(s_runtime_context);
}

//提交本地诊断副本，防止高优先级读取者观察到半更新状态
static void IpcService_UpdateDebug(void)
{
    const IpcCorePort_t *port = IPC_Platform_GetPort();
    uint32_t mask = port->enter();

    g_ipc_runtime_debug = s_runtime.status;
    port->leave(mask);
}
