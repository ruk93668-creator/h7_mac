#include "ipc_service.h"
#include "ipc_platform.h"
#include "ipc_core.h"
#include "framework_init.h"
#include <stddef.h>
#include "framework_timer.h"
#include "platform_time.h"

//记录本核Service是否已经完成一次初始化
static bool s_service_initialized;

//初始化本核IPC状态并关闭05B写入窗口
static SysResult_t IPC_Service_Init(void);

//保存本核唯一运行期上下文，不与其他任务共享写权限
static IpcRuntime_t s_runtime;

//提供Keil观察的运行期诊断副本
volatile IpcRuntimeStatus_t g_ipc_runtime_debug;

//只由私有IPC Timer更新的业务视图副本
volatile IpcApplicationView_t g_ipc_application_debug;

//检查调用方是否为本核预先绑定的运行期执行者
static bool IpcService_IsOwner(void *context);

//在短临界区提交诊断副本，与读取接口配对保护
static void IpcService_UpdateDebug(void);

//保存独立IPC Timer，不依赖OLED Timer
static FrameworkTimerHandle_t s_ipc_timer;

//只在私有Timer回调执行期间允许运行期发布
static bool s_timer_executing;

//由Framework主循环推进本核心跳与对端观察
static void IpcService_Timer(void *context);


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

    //CM4必须先校验头，失败时不向未知布局写错误字段
    result = IPC_Core_Bind(&g_ipc, IPC_Platform_GetPort());
    if (result == SYS_OK)
        result = IPC_Core_InitializeLocal(&ready);
    if (result != SYS_OK)
        return result;

    //本轮不绑定运行期写者，关闭后仅允许一致性读取

    //只绑定预先登记的唯一写者，初始化发布接口仍永久关闭
    result = IPC_Core_ConfigureRuntime(IpcService_IsOwner, NULL);
    if (result != SYS_OK)
        return result;

    result = IPC_Core_EndInitialization();
    if (result != SYS_OK)
        return result;

    //创建独立100ms周期Timer，初始化失败由原Initcall路径返回
    {
        FrameworkTimerConfig_t config = {IPC_HEARTBEAT_PERIOD_MS,
            FRAMEWORK_TIMER_PERIODIC, IpcService_Timer, NULL};
        result = Framework_TimerCreate(&config, &s_ipc_timer);
        if (result != SYS_OK)
            return result;
        result = Framework_TimerStart(s_ipc_timer);
        if (result != SYS_OK)
        {
            (void)Framework_TimerDelete(s_ipc_timer);
            s_ipc_timer = FRAMEWORK_TIMER_INVALID_HANDLE;
            return result;
        }
    }
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

//只允许Framework的私有IPC Timer回调发布
static bool IpcService_IsOwner(void *context)
{
    const IpcCorePort_t *port = IPC_Platform_GetPort();
    (void)context;
    return s_timer_executing && port != NULL && port->is_thread();
}

//在主循环Timer上下文推进心跳，不读取OLED状态
static void IpcService_Timer(void *context)
{
    uint32_t now = Platform_TimeGetMs();
    (void)context;

    if (!s_service_initialized || s_timer_executing)
        return;
    s_timer_executing = true;

    //第一次回调建立本地启动窗口，随后按真实经过时间推进
    if (!s_runtime.initialized)
        (void)IPC_Runtime_Start(&s_runtime, now);
    (void)IPC_Runtime_Process(&s_runtime, now);
    IpcService_UpdateDebug();
    s_timer_executing = false;
}

//提交本地诊断副本，防止高优先级读取者观察到半更新状态
static void IpcService_UpdateDebug(void)
{
    const IpcCorePort_t *port = IPC_Platform_GetPort();
    uint32_t mask = port->enter();

    IpcApplicationView_t view;

    g_ipc_runtime_debug = s_runtime.status;
    (void)IPC_Application_GetView(&s_runtime.peer_application, Platform_TimeGetMs(),
        s_runtime.status.peer_state, s_runtime.status.read_result, &view);
    g_ipc_application_debug = view;
    port->leave(mask);
}

//由主循环获取完整业务副本，不把通信成功等同于业务数据新鲜
SysResult_t IPC_Service_GetApplicationStatus(IpcApplicationView_t *view)
{
    const IpcCorePort_t *port = IPC_Platform_GetPort();
    IpcApplicationCache_t cache;
    uint32_t state, now, mask;
    SysResult_t result;
    uintptr_t address = (uintptr_t)view;

    if (view == NULL || (address & 3U) != 0U ||
        address > UINTPTR_MAX - (sizeof(*view) - 1U))
        return SYS_ERR_INVALID_ARG;
    if (address <= IPC_SHARED_BASE + IPC_SHARED_WINDOW_BYTES - 1U &&
        IPC_SHARED_BASE <= address + sizeof(*view) - 1U)
        return SYS_ERR_INVALID_ARG;
    if (!s_service_initialized || port == NULL || !port->is_thread())
        return SYS_ERR_INVALID_STATE;

    //本地缓存、通信状态和时间在同一边界取样，不直接返回共享指针
    mask = port->enter();
    cache = s_runtime.peer_application;
    state = s_runtime.status.peer_state;
    result = s_runtime.status.read_result;
    now = Platform_TimeGetMs();
    port->leave(mask);
    return IPC_Application_GetView(&cache, now, state, result, view);
}
