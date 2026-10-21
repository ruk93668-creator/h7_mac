#include "app_oled.h"
#include "framework_timer.h"
#include "oled_device.h"
#include <stddef.h>

#define APP_OLED_PERIOD_MS 100U

static OledDevice_t *s_app_oled;
static FrameworkTimerHandle_t s_oled_timer = FRAMEWORK_TIMER_INVALID_HANDLE;

//记录最近一次错误，BUSY表示上一帧尚未结束
static volatile SysResult_t s_oled_error = SYS_OK;

//定时通过设备Ops启动刷新，忙时跳过本次触发
static void App_OledRefresh(void *context)
{
    OledDevice_t *oled = context;
    SysResult_t result = oled->ops->refresh(oled);

    if (result != SYS_OK && result != SYS_ERR_BUSY)
        s_oled_error = result;
}

//初始化OLED应用，依赖DEVICE阶段已完成设备初始化
SysResult_t App_OledInit(void)
{
    Device_t *device;
    OledDevice_t *oled;
    FrameworkTimerConfig_t config;
    SysResult_t result;

    //防止重复创建周期Timer
    if (s_app_oled != NULL)
        return SYS_OK;

    device = Device_Find("oled");
    if (device == NULL)
        return SYS_ERR_NOT_FOUND;
    if (device->state != DEVICE_STATE_READY)
        return SYS_ERR_INVALID_STATE;

    oled = FRAMEWORK_CONTAINER_OF(device, OledDevice_t, base);
    if (oled->ops == NULL || oled->ops->clear == NULL ||
        oled->ops->draw_pixel == NULL || oled->ops->refresh == NULL ||
        oled->ops->process == NULL)
        return SYS_ERR_NOT_SUPPORTED;

    //清空帧缓冲，
    result = oled->ops->clear(oled);

    config.period_ms = APP_OLED_PERIOD_MS;
    config.mode = FRAMEWORK_TIMER_PERIODIC;
    config.callback = App_OledRefresh;
    config.context = oled;

    //创建并启动100ms周期Timer
    result = Framework_TimerCreate(&config, &s_oled_timer);
    if (result != SYS_OK)
        return result;

    result = Framework_TimerStart(s_oled_timer);
    if (result != SYS_OK)
    {
        //启动失败时释放Timer，允许后续重试
        (void)Framework_TimerDelete(s_oled_timer);
        s_oled_timer = FRAMEWORK_TIMER_INVALID_HANDLE;
        return result;
    }

    //全部初始化成功后才允许进入运行轮询
    s_app_oled = oled;
    return SYS_OK;
}

//每轮主循环推进OLED异步刷新
void App_OledRun(void)
{
    SysResult_t result;

    if (s_app_oled == NULL)
        return;

    result = s_app_oled->ops->process(s_app_oled);
    if (result != SYS_OK)
        s_oled_error = result;
}
