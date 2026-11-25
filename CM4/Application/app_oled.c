//OLED应用从IPC Service取得本地快照，只在整屏空闲时更新界面
#include "app_oled.h"
#include "oled_ui.h"
#include "ipc_service.h"
#include "framework_timer.h"
#include <stddef.h>
#include <string.h>

#include "oled_ui_menu.h"
#include "oled_ui_input.h"
#include "oled_ui_tools.h"
#include "platform_time.h"
#include "bsp_u7.h"

//U7方向配置，置1可交换顺时针与逆时针事件
#ifndef APP_OLED_U7_REVERSE
#define APP_OLED_U7_REVERSE 0
#endif

//保存当前页面及各页独立游标
static OledUiPageState_t s_page_state;
//保存主循环待消费的编码器事件
static OledUiInputQueue_t s_oled_input_queue;
//保存菜单过渡及高亮动画的采样状态
static OledUiAnim_t s_oled_anim;
//保存活动弹窗及到期时间
static OledUiPopup_t s_oled_popup;
//保存工具页本地选项、编辑草稿和动画状态
static OledUiTools_t s_oled_tools = { .values = {0, 1, 0, 0, 0, 120} };
//累计队列溢出和切页丢弃的输入事件
volatile uint32_t g_oled_ui_dropped_inputs;

//页面变化后仍在队列中的事件属于旧页面，不能作用到新界面。
static SysResult_t App_OledConsumeInput(void)
{
    uint32_t event;
    uint8_t previous;
    const OledUiPage_t *page;
    uint32_t now = Platform_TimeGetMs();
    SysResult_t result = SYS_OK;

    while ((event = OledUi_InputTake(&s_oled_input_queue)) != 0U)
    {
        if (OledUi_PopupEvent(&s_oled_popup, event, now))
        {
            OledUi_InputDiscard(&s_oled_input_queue);
            break;
        }
        previous = s_page_state.current;
        if (previous == OLED_UI_PAGE_TOOLS)
        {
            OledUiToolsResult_t action = OledUi_ToolsEvent(&s_oled_tools, event, now);
            if (action == OLED_UI_TOOLS_CONTEXT_CHANGED)
            {
                OledUi_InputDiscard(&s_oled_input_queue);
                break;
            }
            if (action == OLED_UI_TOOLS_HANDLED)
                continue;
            event = OLED_UI_EVENT_BACK;
        }
        if (event == OLED_UI_EVENT_SELECT &&
            OledUi_PageGetCurrent(&s_page_state, &page) == SYS_OK &&
            page->type == OLED_UI_PAGE_TYPE_INFO)
        {
            OledUi_PopupOpen(&s_oled_popup, "PAGE INFO", "READ ONLY", true, now, 3000U);
            OledUi_InputDiscard(&s_oled_input_queue);
            break;
        }
        if (event == OLED_UI_EVENT_SELECT && previous == OLED_UI_PAGE_MONITOR)
            event = OLED_UI_EVENT_OPEN_MENU;
        result = OledUi_PageDispatch(&s_page_state, event);
        if (s_page_state.current != previous)
        {
            s_oled_anim.target_pos = 0U;
            if (s_page_state.current == OLED_UI_PAGE_TOOLS)
                s_oled_tools.page_at = now;
            OledUi_InputDiscard(&s_oled_input_queue);
            break;
        }
    }
    g_oled_ui_dropped_inputs = s_oled_input_queue.dropped;
    return result;
}

//U7接线取自调试与扩展页及主控页。C接地，输入使用上拉。
volatile SysResult_t g_oled_u7_result;
//保存U7相位累积和按键去抖状态
static OledUiInput_t s_u7_input;
//上次GPIO采样时刻，单位ms
static uint32_t s_u7_sample_tick;
//BSP初始化成功后才允许读取U7引脚
static bool s_u7_ready;

//初始化板级引脚，采样初始电平以免上电时生成虚假输入
static SysResult_t App_OledU7Init(void)
{
    BspU7Sample_t sample;
    SysResult_t result = BSP_U7_Init();

    if (result != SYS_OK)
        return result;

    sample = BSP_U7_Read();
    s_u7_sample_tick = Platform_TimeGetMs();
    OledUi_InputInit(&s_u7_input, sample.phase, sample.pressed, s_u7_sample_tick);
    s_u7_ready = true;
    return SYS_OK;
}

//每毫秒最多采样一次，去抖与相位解码由UI输入模块完成
static void App_OledU7Poll(void)
{
    BspU7Sample_t sample;
    uint32_t now, event;

    if (!s_u7_ready)
        return;
    now = Platform_TimeGetMs();
    if (now == s_u7_sample_tick)
        return;
    s_u7_sample_tick = now;
    sample = BSP_U7_Read();
    event = OledUi_InputSample(&s_u7_input, sample.phase, sample.pressed, now);
    if (event == 0U)
        return;
    g_oled_u7_result = OledUi_InputPush(&s_oled_input_queue, event, true);
}

//同一个Timer每50ms提供菜单刷新机会，监控仍以100ms限频。
#define APP_OLED_MONITOR_PERIOD_MS 100U
//唯一OLED Timer的调度间隔，单位ms
#define APP_OLED_PERIOD_MS 50U
//上次发起首页绘制的时刻，单位ms
static uint32_t s_oled_monitor_tick;
//上次尝试绘制菜单的时刻，单位ms
static uint32_t s_oled_menu_tick;
//当前菜单是否已有绘制时刻可用于节流
static bool s_oled_menu_attempted;
//忙时只保留一次菜单更新请求
static bool s_oled_menu_pending;
//上次完成四页传输的时刻，单位ms
static uint32_t s_oled_completed_tick;
//相邻完成帧间隔换算的帧率，单位为十分之一FPS
static uint32_t s_oled_fps_tenths;
//相邻两次整帧传输完成的间隔，单位ms
volatile uint32_t g_oled_completed_interval_ms;

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
//处理Timer到期并合并繁忙期间的菜单请求
static void App_OledDeadline(void *context);

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
    config.callback = App_OledDeadline;
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
    (void)OledUi_PageInit(&s_page_state);
    s_oled_monitor_tick = Platform_TimeGetMs();
    s_oled_completed_tick = s_oled_monitor_tick;
    s_oled_input_queue.reverse = APP_OLED_U7_REVERSE != 0;
    g_oled_u7_result = App_OledU7Init();
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
    App_OledU7Poll();
    (void)App_OledConsumeInput();
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
    {
        uint32_t completed_now = Platform_TimeGetMs();
        uint32_t elapsed = completed_now - s_oled_completed_tick;
        s_oled_completed_tick = completed_now;
        if (g_oled_monitor_debug.completed_frames != 0U && elapsed != 0U)
        {
            g_oled_completed_interval_ms = elapsed;
            s_oled_fps_tenths = elapsed >= 10000U ? 0U : 10000U / elapsed;
        }
        g_oled_monitor_debug.completed_frames++;
    }
    //定时器仅产生一个待刷新标记。旧帧完成后再尝试，不补发中间帧。
    if (s_oled_menu_pending && s_page_state.current != OLED_UI_PAGE_MONITOR &&
        s_app_oled->refresh_state == OLED_REFRESH_IDLE)
        App_OledRefresh(s_app_oled);
}

//记录一次菜单绘制请求，等待上帧完成后最多补画一帧
static void App_OledDeadline(void *context)
{
    s_oled_menu_pending = s_page_state.current != OLED_UI_PAGE_MONITOR;
    App_OledRefresh(context);
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

    if (s_page_state.current == OLED_UI_PAGE_MONITOR)
    {
        uint32_t now = Platform_TimeGetMs();

        //先确保设备空闲，再限频监控的IPC取样和绘制。无符号差支持回绕。
        if (now - s_oled_monitor_tick < APP_OLED_MONITOR_PERIOD_MS)
            return;
        s_oled_monitor_tick = now;
        s_oled_menu_attempted = false;
    }
    else
    {
        uint32_t now = Platform_TimeGetMs();

        if (s_oled_menu_attempted && now - s_oled_menu_tick < APP_OLED_PERIOD_MS)
            return;
        s_oled_menu_pending = false;
        s_oled_menu_tick = now;
        s_oled_menu_attempted = true;
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
    if (s_page_state.current == OLED_UI_PAGE_TOOLS)
        result = OledUi_ToolsRender(oled, &s_oled_tools, Platform_TimeGetMs(), s_oled_fps_tenths);
    else if (s_page_state.current != OLED_UI_PAGE_MONITOR)
    {
        const OledUiPage_t *page;
        OledUiMenuStyle_t style;
        OledUiVisual_t visual;
        OledUiStatus_t status;
        OledUiStatusCounters_t counters;
        IpcRuntimeStatus_t runtime;
        uint32_t now = Platform_TimeGetMs();

        OledUi_ToolsReadStyle(&s_oled_tools, &style);
        result = OledUi_PageGetCurrent(&s_page_state, &page);
        if (result == SYS_OK)
        {
            OledUi_AnimSample(&s_oled_anim, &s_page_state, now, page->type == OLED_UI_PAGE_TYPE_MENU ? &style : NULL, &visual);
            counters.completed = g_oled_monitor_debug.completed_frames;
            counters.io_errors = g_oled_monitor_debug.io_errors;
            counters.render_errors = g_oled_monitor_debug.render_errors;
            //仅在About页读取Service维护的本地心跳状态
            counters.peer_heartbeat = 0U;
            counters.heartbeat_valid = page->id == OLED_UI_PAGE_ABOUT &&
                IPC_Service_GetRuntimeStatus(&runtime) == SYS_OK &&
                runtime.peer_state == IPC_PEER_ONLINE && runtime.progress_seen != 0U;
            if (counters.heartbeat_valid)
                counters.peer_heartbeat = runtime.peer_heartbeat;
            result = OledUi_MenuRenderVisual(oled, &s_page_state, &visual,
                (OledUi_StatusBuild(page->id, &view, &counters, &status) ||
                 //公共IPC尚未发布任务诊断，NULL在窗口显示为未发布。
                 OledUi_SystemBuild(page->id, &view, NULL, NULL, now, &status)) ? &status : NULL, &style);
        }
        OledUi_PopupTick(&s_oled_popup, now);
        if (result == SYS_OK)
            result = OledUi_MenuPopup(oled, &s_oled_popup, now, &style);
    }
    else
        result = OledUi_Render(oled, &view);
    //首页第三行的DROP文本最多15字符，右侧空间可叠加帧率且不遮住监控内容
    if (result == SYS_OK && s_page_state.current != OLED_UI_PAGE_TOOLS)
        result = OledUi_ToolsApplyDisplay(oled, &s_oled_tools, s_oled_fps_tenths,
            s_page_state.current == OLED_UI_PAGE_MONITOR ? 16 : 0);
    if (result != SYS_OK)
    {
        memcpy(oled->framebuffer, s_frame_backup, sizeof(s_frame_backup));
        g_oled_monitor_debug.render_errors++;
        if (s_page_state.current != OLED_UI_PAGE_MONITOR && !s_oled_popup.active)
            OledUi_PopupOpen(&s_oled_popup, "RENDER ERROR", "RETRYING", false,
                Platform_TimeGetMs(), 1800U);
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
