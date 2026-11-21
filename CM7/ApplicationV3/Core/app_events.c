#include "app_events.h"
#include "usb_device.h"
#include "camera_capture.h"
#include "platform_sync.h"
#include <stddef.h>

//保存各业务目标对应的静态任务句柄
static TaskHandle_t s_tasks[RUNTIME_TARGET_COUNT];

//合并BOOT发布前收到的提示位，不提前唤醒业务
static uint32_t s_pending[RUNTIME_TARGET_COUNT];

//记录事件后端是否已绑定
static bool s_initialized;

//记录main是否已提交全部初始化成功结果
static bool s_prepared;

//锁存准备失败，失败后不再允许发布BOOT
static bool s_failed;

//记录启动屏障是否已经提交
static volatile bool s_boot_ready;

//向目标任务发送普通上下文事件提示
static SysResult_t AppEvents_Post(void *context, RuntimeEventTarget_t target, uint32_t events);

//从外设中断通知目标任务
static SysResult_t AppEvents_PostFromIsr(void *context, RuntimeEventTarget_t target, uint32_t events);

//将USB状态变化转成任务唤醒提示
static void AppEvents_UsbNotify(uint32_t events, void *context);

//检查事件状态并锁存启动前提示
static SysResult_t AppEvents_PreparePost(RuntimeEventTarget_t target, uint32_t events, bool *deferred);

//将Media或BSP进度转成Camera任务通知
static void AppEvents_CameraNotify(uint32_t events, void *context);

//向Common绑定普通C回调表，隐藏FreeRTOS任务类型
static const RuntimeEventPort_t s_event_port =
{
    .post = AppEvents_Post,
    .post_from_isr = AppEvents_PostFromIsr,
    .context = NULL
};

//准备事件适配和USB状态通知
SysResult_t App_EventsInit(void)
{
    SysResult_t result;

    //只允许调度器启动前的初始化流程绑定后端
    if (Platform_SyncGetException() != 0U || xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
        return SYS_ERR_INVALID_STATE;
    if (s_initialized)
        return SYS_ERR_ALREADY_EXISTS;

    //先接入USB普通C通知，回调不处理协议或图像
    result = USB_Device_SetEventCallback(AppEvents_UsbNotify, NULL);
    if (result != SYS_OK)
        return result;

    //绑定静态回调表，后续提示由Application适配到任务通知
    //绑定采集进度和异常提示，两个回调都不执行Media任务侧操作
    result = CameraCapture_SetEventCallback(AppEvents_CameraNotify, NULL);
    if (result != SYS_OK)
        return result;

    result = RuntimeEvent_Bind(&s_event_port);
    if (result != SYS_OK)
        return result;

    //两处绑定都成功后才提交已初始化状态
    s_initialized = true;
    return SYS_OK;
}

//登记指定目标的任务句柄
SysResult_t App_EventsAttach(RuntimeEventTarget_t target, TaskHandle_t task)
{

    //检查目标范围和任务句柄，避免无效数组访问
    if ((uint32_t)target >= RUNTIME_TARGET_COUNT || task == NULL)
        return SYS_ERR_INVALID_ARG;

    //只能在准备阶段登记，失败或调度运行后禁止修改接收关系
    if (!s_initialized || s_failed || Platform_SyncGetException() != 0U ||
        xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
        return SYS_ERR_INVALID_STATE;

    //同一目标只能绑定一个任务
    if (s_tasks[target] != NULL)
        return SYS_ERR_ALREADY_EXISTS;

    //保存句柄，不复制或接管任务的静态内存
    s_tasks[target] = task;
    return SYS_OK;
}

//记录初始化结果并准备启动条件
SysResult_t App_EventsRecordInitialization(SysResult_t result)
{
    uint32_t i;

    //初始化结果只允许在调度器启动前提交一次
    if (!s_initialized || s_prepared || s_failed || Platform_SyncGetException() != 0U ||
        xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
        return SYS_ERR_INVALID_STATE;

    //锁存main传入的失败，不把部分准备成功当作整体成功
    if (result != SYS_OK)
    {
        s_failed = true;
        return result;
    }

    //逐一确认所有通知接收端都已创建
    for (i = 0U; i < RUNTIME_TARGET_COUNT; i++)
    {
        if (s_tasks[i] == NULL)
            return SYS_ERR_INVALID_STATE;
    }

    //条件全部成立后，只标记准备完成，暂不发布BOOT
    s_prepared = true;
    return SYS_OK;
}

//向全部业务任务发布启动屏障
SysResult_t App_EventsReleaseBoot(void)
{
    uint32_t i;

    //仅允许调度器运行后的Control任务放行，main不能提前广播
    if (Platform_SyncGetException() != 0U || !s_prepared || s_failed ||
        xTaskGetSchedulerState() != taskSCHEDULER_RUNNING ||
        xTaskGetCurrentTaskHandle() != s_tasks[RUNTIME_TARGET_CONTROL])
        return SYS_ERR_INVALID_STATE;

    //启动屏障只能发布一次
    if (s_boot_ready)
        return SYS_ERR_ALREADY_EXISTS;

    //先完整提交屏障，再让高优先级任务执行
    taskENTER_CRITICAL();
    s_boot_ready = true;
    for (i = 0U; i < RUNTIME_TARGET_COUNT; i++)
    {

        //把BOOT与早期锁存提示一起送出，重复提示位允许合并
        (void)xTaskNotify(s_tasks[i], RUNTIME_EVENT_BOOT_READY | s_pending[i], eSetBits);
        s_pending[i] = 0U;
    }

    //全部接收端通知完成后退出临界区，允许高优先级任务执行
    taskEXIT_CRITICAL();
    return SYS_OK;
}

//阻塞等待当前任务的事件提示
SysResult_t App_EventsWait(RuntimeEventTarget_t target, uint32_t timeout_ms, uint32_t *events)
{
    TickType_t ticks;

    //检查目标和结果指针，防止无效输出
    if ((uint32_t)target >= RUNTIME_TARGET_COUNT || events == NULL)
        return SYS_ERR_INVALID_ARG;

    //只能由对应任务在可阻塞环境中等待，ISR或屏蔽状态不能调用
    if (Platform_SyncGetException() != 0U || Platform_SyncGetPrimask() != 0U || Platform_SyncGetBasepri() != 0U ||
        xTaskGetSchedulerState() != taskSCHEDULER_RUNNING ||
        xTaskGetCurrentTaskHandle() != s_tasks[target])
        return SYS_ERR_INVALID_STATE;

    //把毫秒转换为内核Tick，永久等待使用portMAX_DELAY
    ticks = timeout_ms == APP_EVENTS_WAIT_FOREVER ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);

    //非零等待不足一个Tick时至少等待一个Tick
    if (timeout_ms != 0U && ticks == 0U)
        ticks = 1U;

    //先清空本次输出；内核通知入口不清位，避免丢失先到达的提示
    *events = 0U;

    //不清入口通知位，保留等待前已经到达的事件
    return xTaskNotifyWait(0U, UINT32_MAX, events, ticks) == pdTRUE
        ? SYS_OK : SYS_ERR_TIMEOUT;
}

//检查事件状态并锁存启动前提示
static SysResult_t AppEvents_PreparePost(RuntimeEventTarget_t target, uint32_t events, bool *deferred)
{

    //保存原始全局屏蔽状态，退出时必须按原值恢复
    uint32_t primask = Platform_SyncGetPrimask();
    SysResult_t result = SYS_OK;

    //短暂屏蔽中断，使BOOT判断与早期提示合并保持一致
    Platform_SyncDisableIrq();
    if (!s_initialized || s_failed)
        result = SYS_ERR_INVALID_STATE;
    else if (!s_boot_ready)
    {

        //屏障未发布时只累计提示，不调用内核唤醒业务
        s_pending[target] |= events;
        *deferred = true;
    }

    //恢复进入前的中断状态，不擅自开启调用者关闭的中断
    Platform_SyncRestoreIrq(primask);
    return result;
}

//向目标任务发送普通上下文事件提示
static SysResult_t AppEvents_Post(void *context, RuntimeEventTarget_t target, uint32_t events)
{
    bool deferred = false;
    SysResult_t result;
    (void)context;

    //普通接口拒绝ISR；BOOT后也拒绝屏蔽内核中断的调用环境
    if (Platform_SyncGetException() != 0U || Platform_SyncGetPrimask() != 0U ||
        (s_boot_ready && Platform_SyncGetBasepri() != 0U))
        return SYS_ERR_INVALID_STATE;

    //先检查准备状态，BOOT前的提示在这里被锁存
    result = AppEvents_PreparePost(target, events, &deferred);
    if (result != SYS_OK || deferred)
        return result;

    //用位合并通知唤醒对应任务，不保证重复提示的次数
    return xTaskNotify(s_tasks[target], events, eSetBits) == pdPASS ? SYS_OK : SYS_ERR_IO;
}

//从外设中断通知目标任务
static SysResult_t AppEvents_PostFromIsr(void *context, RuntimeEventTarget_t target, uint32_t events)
{

    //取得当前异常号，用于区分外设IRQ并查询其优先级
    uint32_t ipsr = Platform_SyncGetException();
    BaseType_t woken = pdFALSE;
    bool deferred = false;
    SysResult_t result;
    (void)context;

    //仅外设IRQ，优先级数值必须不小于内核API阈值
    if (ipsr < 16U || Platform_SyncGetIrqPriority(ipsr - 16U) <
        configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY)
        return SYS_ERR_INVALID_STATE;

    //BOOT前只缓存提示，不能在调度器尚未就绪时唤醒任务
    result = AppEvents_PreparePost(target, events, &deferred);
    if (result != SYS_OK || deferred)
        return result;

    //使用ISR专用接口，并取得是否需要唤醒更高优先级任务
    if (xTaskNotifyFromISR(s_tasks[target], events, eSetBits, &woken) != pdPASS)
        return SYS_ERR_IO;

    //需要时请求PendSV，退出中断后由内核完成任务切换
    portYIELD_FROM_ISR(woken);
    return SYS_OK;
}

//将USB状态变化转成任务唤醒提示
static void AppEvents_UsbNotify(uint32_t events, void *context)
{
    (void)events;
    (void)context;

    //USB回调可能来自初始化或IRQ，按实际上下文选择通知接口
    if (Platform_SyncGetException() != 0U)
        (void)RuntimeEvent_PostFromIsr(RUNTIME_TARGET_USB, RUNTIME_EVENT_USB_PROGRESS);
    else
        (void)RuntimeEvent_Post(RUNTIME_TARGET_USB, RUNTIME_EVENT_USB_PROGRESS);
}

//检查当前执行者是否为目标任务
bool App_EventsIsOwner(RuntimeEventTarget_t target)
{
    return (uint32_t)target < RUNTIME_TARGET_COUNT && Platform_SyncGetException() == 0U &&
        Platform_SyncGetPrimask() == 0U && Platform_SyncGetBasepri() == 0U &&
        xTaskGetSchedulerState() == taskSCHEDULER_RUNNING &&
        xTaskGetCurrentTaskHandle() == s_tasks[target];
}

//将采集提示转成Camera任务唤醒
static void AppEvents_CameraNotify(uint32_t events, void *context)
{
    (void)events;
    (void)context;

    //BSP或Media沿用当前上下文，ISR只发通知，不停止硬件
    if (Platform_SyncGetException() != 0U)
        (void)RuntimeEvent_PostFromIsr(RUNTIME_TARGET_CAMERA, RUNTIME_EVENT_CAMERA_PROGRESS);
    else
        (void)RuntimeEvent_Post(RUNTIME_TARGET_CAMERA, RUNTIME_EVENT_CAMERA_PROGRESS);
}
