//OLED应用从IPC Service取得本地快照，只在整屏空闲时更新界面
#include "app_oled.h"
#include "oled_ui.h"
#include "ipc_service.h"
#include "framework_timer.h"
#include <stddef.h>
#include <string.h>

//沿用原有100ms周期，不另建任务或第二个刷新Timer
#define APP_OLED_PERIOD_MS 100U

//保存已经就绪的OLED设备，不直接操作BSP或HAL
static OledDevice_t *s_app_oled;

//保存唯一的周期Timer，重复初始化时不重新创建
static FrameworkTimerHandle_t s_oled_timer = FRAMEWORK_TIMER_INVALID_HANDLE;

//绘图前备份原Framebuffer，绘图失败或提交BUSY时可以回滚
static uint8_t s_frame_backup[OLED_BUFFER_SIZE];

//只记录真实错误，BUSY不是永久故障
static volatile SysResult_t s_oled_error = SYS_OK;

//保存本地刷新和IPC诊断，多个Watch字段不保证调试器同时取样
volatile AppOledDebug_t g_oled_monitor_debug;

//在Timer回调中取快照、绘图并尝试启动整屏刷新
static void App_OledRefresh(void *context);

//保存真实失败，供主循环和Keil观察
static void App_OledRecordError(SysResult_t result);

//初始化OLED应用并建立周期刷新
SysResult_t App_OledInit(void)
{
    Device_t *device;
    OledDevice_t *oled;
    FrameworkTimerConfig_t config;
    SysResult_t result;

    //应用已就绪时直接返回，不重复分配Timer
    if (s_app_oled != NULL)
        return SYS_OK;

    //依赖Device阶段完成注册和初始化，不能操作未准备好的屏幕
    device = Device_Find("oled");
    if (device == NULL)
        return SYS_ERR_NOT_FOUND;

    if (device->state != DEVICE_STATE_READY)
        return SYS_ERR_INVALID_STATE;

    //绘图和刷新都通过原Device Ops，Application不依赖IIC实现
    oled = FRAMEWORK_CONTAINER_OF(device, OledDevice_t, base);
    if (oled->ops == NULL || oled->ops->clear == NULL ||
        oled->ops->draw_pixel == NULL || oled->ops->refresh == NULL ||
        oled->ops->process == NULL)
        return SYS_ERR_NOT_SUPPORTED;

    //清屏也必须避开发送期间，失败时不能假装应用初始化成功
    if (oled->refresh_state != OLED_REFRESH_IDLE)
        return SYS_ERR_BUSY;

    result = oled->ops->clear(oled);
    if (result != SYS_OK)
        return result;

    //保留原周期与回调方式，IPC自己的Timer独立运行
    config.period_ms = APP_OLED_PERIOD_MS;
    config.mode = FRAMEWORK_TIMER_PERIODIC;
    config.callback = App_OledRefresh;
    config.context = oled;
    result = Framework_TimerCreate(&config, &s_oled_timer);
    if (result != SYS_OK)
        return result;

    //启动失败回收Timer，允许之后重新初始化
    result = Framework_TimerStart(s_oled_timer);
    if (result != SYS_OK)
    {
        (void)Framework_TimerDelete(s_oled_timer);
        s_oled_timer = FRAMEWORK_TIMER_INVALID_HANDLE;
        return result;
    }

    //全部准备成功后才允许主循环推进刷新
    s_app_oled = oled;
    return SYS_OK;
}

//每轮主循环推进OLED异步刷新
void App_OledRun(void)
{
    OledRefreshState_t previous;
    SysResult_t result;

    //尚未初始化时不调用设备或IPC接口
    if (s_app_oled == NULL)
        return;

    //Process只推进传输，不在发送期间重绘Framebuffer
    previous = s_app_oled->refresh_state;
    result = s_app_oled->ops->process(s_app_oled);
    if (result == SYS_ERR_BUSY)
    {
        g_oled_monitor_debug.busy_skips++;
        return;
    }

    //真实传输失败单独累计，下一周期仍可尝试更新界面
    if (result != SYS_OK)
    {
        g_oled_monitor_debug.io_errors++;
        App_OledRecordError(result);
        return;
    }

    //只有最后一页成功完成并回到IDLE，才计为整屏完成
    if (previous == OLED_REFRESH_WAIT_DATA &&
        s_app_oled->refresh_state == OLED_REFRESH_IDLE)
        g_oled_monitor_debug.completed_frames++;
}

//周期取得IPC副本并启动一轮完整界面刷新
static void App_OledRefresh(void *context)
{
    OledDevice_t *oled = context;
    IpcApplicationView_t view;
    SysResult_t result;

    //上一帧仍在发送时跳过，不能先清屏再发现设备忙
    if (oled->refresh_state != OLED_REFRESH_IDLE)
    {
        g_oled_monitor_debug.busy_skips++;
        return;
    }

    //整屏使用一份完整本地视图，禁止直接访问g_ipc共享对象
    result = IPC_Service_GetApplicationStatus(&view);
    g_oled_monitor_debug.ipc_result = result;
    if (result != SYS_OK)
    {
        g_oled_monitor_debug.ipc_errors++;
        App_OledRecordError(result);
        return;
    }

    //保留本轮界面的有效性依据，方便与IPC Watch值核对
    g_oled_monitor_debug.peer_state = view.peer_state;
    g_oled_monitor_debug.valid_mask = view.valid_mask;
    g_oled_monitor_debug.fresh_mask = view.fresh_mask;

    //只在IDLE备份和绘图，后续刷新成功后这份Framebuffer一直保持不变
    memcpy(s_frame_backup, oled->framebuffer, sizeof(s_frame_backup));
    result = OledUi_Render(oled, &view);
    if (result != SYS_OK)
    {
        memcpy(oled->framebuffer, s_frame_backup, sizeof(s_frame_backup));
        g_oled_monitor_debug.render_errors++;
        App_OledRecordError(result);
        return;
    }

    //绘制完成才提交第一页，成功后交给主循环推进全部四页
    result = oled->ops->refresh(oled);
    g_oled_monitor_debug.last_result = result;
    if (result == SYS_OK)
    {
        g_oled_monitor_debug.submitted_frames++;
        return;
    }

    //提交未成功时驱动未借用Framebuffer，恢复原画面避免BUSY留下半次更新
    memcpy(oled->framebuffer, s_frame_backup, sizeof(s_frame_backup));
    if (result == SYS_ERR_BUSY)
    {
        g_oled_monitor_debug.busy_skips++;
        return;
    }

    //真实IIC失败保留错误结果，不把BUSY混入永久故障记录
    g_oled_monitor_debug.io_errors++;
    App_OledRecordError(result);
}

//保存最近一次真实错误
static void App_OledRecordError(SysResult_t result)
{

    //忙只表示稍后重试，不覆盖真实故障记录
    if (result == SYS_ERR_BUSY)
        return;

    s_oled_error = result;
    g_oled_monitor_debug.last_result = result;
    g_oled_monitor_debug.last_error = result;
}
