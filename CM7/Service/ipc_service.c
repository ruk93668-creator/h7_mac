#include "ipc_service.h"
#include "ipc_platform.h"
#include "ipc_core.h"
#include "framework_init.h"
#include <stddef.h>

//记录本核Service是否已经完成一次初始化
static bool s_service_initialized;

//初始化本核IPC状态并关闭初始化写入窗口
static SysResult_t IPC_Service_Init(void);

//保存本核唯一运行期上下文，不与其他任务共享写权限
static IpcRuntime_t s_runtime;

//保存静态身份回调，初始化结束后不允许替换
static bool (*s_runtime_owner)(void *context);

//保存身份回调使用的本核上下文，不写入共享内存
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

//初始化本核IPC状态并关闭初始化写入窗口
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

    //只绑定预先登记的唯一写者，初始化发布接口仍永久关闭
    if (s_runtime_owner != NULL)
    {
        result = IPC_Core_ConfigureRuntime(IpcService_IsOwner, NULL);
        if (result != SYS_OK)
            return result;
    }

    //关闭初始化发布入口，此后只能由授权执行者运行期发布
    result = IPC_Core_EndInitialization();
    if (result != SYS_OK)
        return result;

    //所有准备步骤成功后才标记初始化完成
    s_service_initialized = true;
    return SYS_OK;
}

//取得本地诊断的完整副本
SysResult_t IPC_Service_GetRuntimeStatus(IpcRuntimeStatus_t *status)
{
    const IpcCorePort_t *port = IPC_Platform_GetPort();
    uint32_t mask;

    //检查输出缓冲区及地址溢出，确保完整诊断副本可安全写入
    if (status == NULL || ((uintptr_t)status & 3U) != 0U ||
        (uintptr_t)status > UINTPTR_MAX - (sizeof(*status) - 1U))
        return SYS_ERR_INVALID_ARG;

    //输出必须留在本核内存，禁止通过读取接口覆盖共享状态
    if ((uintptr_t)status <= IPC_SHARED_BASE + IPC_SHARED_WINDOW_BYTES - 1U &&
        IPC_SHARED_BASE <= (uintptr_t)status + sizeof(*status) - 1U)
        return SYS_ERR_INVALID_ARG;

    //拒绝中断等不允许的调用环境，保持任务侧接口约定
    //拒绝非线程上下文，避免从ISR调用任务侧读取或授权接口
    if (port == NULL || !port->is_thread())
        return SYS_ERR_INVALID_STATE;

    //短临界区复制完整本地诊断，避免读取过程中被更新
    mask = port->enter();
    *status = g_ipc_runtime_debug;
    port->leave(mask);
    return SYS_OK;
}

//登记Application提供的静态任务身份检查
SysResult_t IPC_Service_SetRuntimeOwner(bool (*is_owner)(void *context), void *context)
{
    const IpcCorePort_t *port = IPC_Platform_GetPort();

    //身份检查回调不能为空，否则无法限制唯一发布者
    if (is_owner == NULL)
        return SYS_ERR_INVALID_ARG;

    //授权依据必须保存在本核，不能依赖对端可变的共享内容
    if (context != NULL && (uintptr_t)context >= IPC_SHARED_BASE &&
        (uintptr_t)context < IPC_SHARED_BASE + IPC_SHARED_WINDOW_BYTES)
        return SYS_ERR_INVALID_ARG;

    //拒绝非线程上下文，避免从ISR调用任务侧读取或授权接口
    if (port == NULL || !port->is_thread())
        return SYS_ERR_INVALID_STATE;

    //初始化后禁止换写者，重复绑定也不能覆盖原有授权
    if (s_service_initialized || s_runtime_owner != NULL)
        return SYS_ERR_INVALID_STATE;

    //成对保存回调与上下文，随后由Core冻结发布授权
    s_runtime_owner = is_owner;
    s_runtime_context = context;
    return SYS_OK;
}

//在BOOT放行后建立本核运行期基线
SysResult_t IPC_Service_RuntimeStart(uint32_t now)
{

    //初始化完成且调用者身份正确，才允许运行期操作
    if (!s_service_initialized || !IpcService_IsOwner(NULL))
        return SYS_ERR_INVALID_STATE;
    return IPC_Runtime_Start(&s_runtime, now);
}

//由唯一IPC Task推进一次运行期处理
SysResult_t IPC_Service_RuntimeProcess(uint32_t now)
{
    SysResult_t result;

    //初始化完成且调用者身份正确，才允许运行期操作
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

//只在唯一IPC Task中更新待发布的业务载荷
SysResult_t IPC_Service_SetApplication(const IpcApplicationStatus_t *application, uint32_t sampled_tick)
{
    uintptr_t address = (uintptr_t)application;

    //先校验载荷地址及对齐，防止复制非法内存
    if (application == NULL || (address & 3U) != 0U ||
        address > UINTPTR_MAX - (sizeof(*application) - 1U))
        return SYS_ERR_INVALID_ARG;

    //载荷必须来自本核副本，禁止绕过单写者直接使用共享区
    if (address <= IPC_SHARED_BASE + IPC_SHARED_WINDOW_BYTES - 1U &&
        IPC_SHARED_BASE <= address + sizeof(*application) - 1U)
        return SYS_ERR_INVALID_ARG;

    //只有已启动的唯一IPC任务可以更新待发布业务数据
    if (!s_service_initialized || !s_runtime.initialized || !IpcService_IsOwner(NULL))
        return SYS_ERR_INVALID_STATE;

    //校验业务字段和来源位，拒绝不符合协议的快照
    if (!IPC_Application_Validate(application))
        return SYS_ERR_INVALID_ARG;

    //连同采样时刻保存副本，发布时补入采样后的本核等待时间
    s_runtime.application = *application;
    s_runtime.application_tick = sampled_tick;
    s_runtime.application_timed = true;
    return SYS_OK;
}
