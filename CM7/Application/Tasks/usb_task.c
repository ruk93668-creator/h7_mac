#include "usb_task.h"
#include "app_events.h"
#include "usb_mode.h"
#include <stddef.h>
#include "stm32h7xx.h"

#define USB_TASK_STACK_WORDS 2048U
#define USB_TASK_PRIORITY    4U
#define USB_TASK_WAIT_MS     20U

static StaticTask_t s_usb_tcb;
static StackType_t s_usb_stack[USB_TASK_STACK_WORDS] __attribute__((aligned(8)));
static TaskHandle_t s_usb_task;
static bool s_usb_started;
static bool s_usb_fault_reported;
volatile UsbTaskDebug_t g_usb_task_debug;

//UsbTask独占TEST连接与模式处理，不调用UVC或Media
static void UsbTask_Run(void *argument);

//仅在收到启动屏障后启动一次USB
static void UsbTask_Process(uint32_t events);

SysResult_t UsbTask_Create(void)
{
    if (__get_IPSR() != 0U || xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
        return SYS_ERR_INVALID_STATE;
    if (s_usb_task != NULL)
        return SYS_ERR_ALREADY_EXISTS;

    s_usb_task = xTaskCreateStatic(UsbTask_Run, "Usb", USB_TASK_STACK_WORDS,
        NULL, USB_TASK_PRIORITY, s_usb_stack, &s_usb_tcb);
    if (s_usb_task == NULL)
        return SYS_ERR_NO_RESOURCE;

    return App_EventsAttach(RUNTIME_TARGET_USB, s_usb_task);
}

//内部功能函数实现
static void UsbTask_Run(void *argument)
{
    uint32_t events;
    SysResult_t result;
    (void)argument;

    for (;;)
    {
        result = App_EventsWait(RUNTIME_TARGET_USB,
            g_usb_task_debug.boot_seen != 0U ? USB_TASK_WAIT_MS : APP_EVENTS_WAIT_FOREVER, &events);
        if (result == SYS_ERR_TIMEOUT)
            g_usb_task_debug.timeout_count++;
        else if (result != SYS_OK)
        {
            g_usb_task_debug.result = result;
            configASSERT(result == SYS_OK);
            continue;
        }

        UsbTask_Process(events);
        g_usb_task_debug.stack_free_words = uxTaskGetStackHighWaterMark(NULL);
    }
}

static void UsbTask_Process(uint32_t events)
{
    if (g_usb_task_debug.boot_seen == 0U)
    {
        if ((events & RUNTIME_EVENT_BOOT_READY) == 0U)
            return;

        g_usb_task_debug.boot_seen = 1U;
        g_usb_task_debug.result = USB_Mode_GetMode() == USB_MODE_TEST
            ? USB_Mode_Start() : SYS_ERR_NOT_SUPPORTED;
        s_usb_started = g_usb_task_debug.result == SYS_OK;
        if (!s_usb_started)
        {
            (void)RuntimeEvent_Post(RUNTIME_TARGET_CONTROL, RUNTIME_EVENT_FAULT);
            s_usb_fault_reported = true;
        }
    }

    g_usb_task_debug.wake_count++;
    if (s_usb_started)
    {
        USB_Mode_Process();
        g_usb_task_debug.result = USB_Mode_GetLastResult();
        if (g_usb_task_debug.result != SYS_OK && !s_usb_fault_reported)
        {
            (void)RuntimeEvent_Post(RUNTIME_TARGET_CONTROL, RUNTIME_EVENT_FAULT);
            s_usb_fault_reported = true;
        }
    }
}
