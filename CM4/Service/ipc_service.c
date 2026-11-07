#include "ipc_service.h"
#include "ipc_platform.h"
#include "ipc_core.h"
#include "framework_init.h"
#include <stddef.h>
#include "framework_timer.h"
#include "platform_time.h"

//记录本核Service是否已经完成一次初始化
static bool s_service_initialized;

//初始化本核IPC状态并关闭初始化写入窗口
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

//初始化本核IPC状态并关闭初始化写入窗口
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

    //只绑定预先登记的唯一写者，初始化发布接口仍永久关闭
    result = IPC_Core_ConfigureRuntime(IpcService_IsOwner, NULL);
    if (result != SYS_OK)
        return result;

    //关闭初始化发布入口，此后只能由授权执行者运行期发布
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

        //创建成功后才启动周期推进，避免使用无效Timer句柄
        result = Framework_TimerStart(s_ipc_timer);
        if (result != SYS_OK)
        {

            //启动失败时回收已创建的Timer，保留原始失败结果
            (void)Framework_TimerDelete(s_ipc_timer);
            s_ipc_timer = FRAMEWORK_TIMER_INVALID_HANDLE;
            return result;
        }
    }

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

    //拒绝非线程上下文，避免从ISR调用任务侧读取或授权接口
    if (port == NULL || !port->is_thread())
        return SYS_ERR_INVALID_STATE;

    //短临界区复制完整本地诊断，避免读取过程中被更新
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

    //未初始化或已在回调中时直接返回，防止重复进入发布路径
    if (!s_service_initialized || s_timer_executing)
        return;

    //仅在私有回调期间开放发布身份，不向其他主循环模块授权
    s_timer_executing = true;

    //第一次回调建立本地启动窗口，随后按真实经过时间推进
    if (!s_runtime.initialized)
        (void)IPC_Runtime_Start(&s_runtime, now);

    //推进心跳和对端观察，不依赖OLED刷新是否成功
    (void)IPC_Runtime_Process(&s_runtime, now);
    IpcService_UpdateDebug();

    //处理结束即收回发布身份，其他调用者不能复用该权限
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

    //检查输出空间及对齐，避免完整视图写入非法地址
    if (view == NULL || (address & 3U) != 0U ||
        address > UINTPTR_MAX - (sizeof(*view) - 1U))
        return SYS_ERR_INVALID_ARG;

    //只向本核普通内存交付副本，禁止覆盖共享窗口
    if (address <= IPC_SHARED_BASE + IPC_SHARED_WINDOW_BYTES - 1U &&
        IPC_SHARED_BASE <= address + sizeof(*view) - 1U)
        return SYS_ERR_INVALID_ARG;

    //只在Service就绪后的正常线程上下文读取业务视图
    if (!s_service_initialized || port == NULL || !port->is_thread())
        return SYS_ERR_INVALID_STATE;

    //本地缓存、通信状态和时间在同一边界取样，不直接返回共享指针
    mask = port->enter();
    cache = s_runtime.peer_application;
    state = s_runtime.status.peer_state;
    result = s_runtime.status.read_result;
    now = Platform_TimeGetMs();
    port->leave(mask);

    //退出临界区后计算新鲜度，上层只接触完整本地副本
    return IPC_Application_GetView(&cache, now, state, result, view);
}
