#include "platform_rtos.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stm32h7xx.h"
#include <stddef.h>

//栈深度单位为32位Word，业务任务8KB，空闲任务1KB
#define RTOS_LEGACY_STACK_WORDS 2048U
#define RTOS_LEGACY_PRIORITY    3U

static StaticTask_t s_legacy_tcb;
static StackType_t s_legacy_stack[RTOS_LEGACY_STACK_WORDS];
static StaticTask_t s_idle_tcb;
static StackType_t s_idle_stack[configMINIMAL_STACK_SIZE];
static TaskHandle_t s_legacy_task;
static void (*s_run_once)(void);
volatile PlatformRtosDebug_t g_rtos_debug;

//在唯一业务任务中保持原Main_Run调用顺序
static void Rtos_LegacyTask(void *argument);

SysResult_t Platform_RtosStart(void (*run_once)(void))
{
    if (run_once == NULL)
        return SYS_ERR_INVALID_ARG;
    if (__get_IPSR() != 0U || __get_PRIMASK() != 0U || s_legacy_task != NULL)
        return SYS_ERR_INVALID_STATE;

    //仅使用抢占优先级，不引入子优先级
    configASSERT(__NVIC_PRIO_BITS == configPRIO_BITS);
    configASSERT(NVIC_GetPriorityGrouping() == 3U);
    s_run_once = run_once;
    s_legacy_task = xTaskCreateStatic(Rtos_LegacyTask, "LegacyVideo",
        RTOS_LEGACY_STACK_WORDS, NULL, RTOS_LEGACY_PRIORITY,
        s_legacy_stack, &s_legacy_tcb);
    if (s_legacy_task == NULL)
        return SYS_ERR_NO_RESOURCE;

    //内核接管PSP和异常调度，返回表示启动失败
    vTaskStartScheduler();
    return SYS_ERR_INVALID_STATE;
}

void Platform_RtosRecordFault(void)
{
    g_rtos_debug.cfsr = SCB->CFSR;
    g_rtos_debug.hfsr = SCB->HFSR;
}

void Platform_RtosAssert(const char *file, uint32_t line)
{
    __disable_irq();
    g_rtos_debug.fault_file = file;
    g_rtos_debug.fault_line = line;
    Platform_RtosRecordFault();
    while (1) { }
}

//由内核取得空闲任务的静态TCB和栈
void vApplicationGetIdleTaskMemory(StaticTask_t **tcb, StackType_t **stack, uint32_t *size)
{
    *tcb = &s_idle_tcb;
    *stack = s_idle_stack;
    *size = configMINIMAL_STACK_SIZE;
}

//Tick Hook仅计数，不调用HAL或业务处理
void vApplicationTickHook(void)
{
    g_rtos_debug.tick_count++;
}

//业务任务阻塞后空闲任务应能持续获得运行机会
void vApplicationIdleHook(void)
{
    g_rtos_debug.idle_iterations++;
    __WFI();
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    Platform_RtosAssert(name, __LINE__);
}

//内部功能函数实现
static void Rtos_LegacyTask(void *argument)
{
    uint32_t sample = 0U;
    (void)argument;
    g_rtos_debug.started = 1U;
    for (;;)
    {
        s_run_once();
        g_rtos_debug.legacy_iterations++;

        //每1000轮采样栈余量，避免每轮扫描整个栈
        if (++sample >= 1000U)
        {
            sample = 0U;
            g_rtos_debug.legacy_stack_free_words = uxTaskGetStackHighWaterMark(NULL);
            g_rtos_debug.idle_stack_free_words = uxTaskGetStackHighWaterMark(xTaskGetIdleTaskHandle());
        }

        //每轮等待下一个Tick，不让高优先级任务无限忙等
        vTaskDelay(1U);
    }
}
