#include "platform_rtos.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stm32h7xx.h"
#include <stddef.h>
#include <stdbool.h>

//Platform仅提供调度器环境和内核Hook，不创建业务任务
static StaticTask_t s_idle_tcb;
static StackType_t s_idle_stack[configMINIMAL_STACK_SIZE] __attribute__((aligned(8)));
static bool s_start_attempted;
volatile PlatformRtosDebug_t g_rtos_debug;

SysResult_t Platform_RtosStart(void)
{
    if (__get_IPSR() != 0U || __get_PRIMASK() != 0U ||
        s_start_attempted || xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED ||
        uxTaskGetNumberOfTasks() == 0U)
        return SYS_ERR_INVALID_STATE;

    //创建任务后内核可能保持BASEPRI屏蔽，由首个任务启动流程恢复
    configASSERT(__NVIC_PRIO_BITS == configPRIO_BITS);
    configASSERT(NVIC_GetPriorityGrouping() == 3U);
    s_start_attempted = true;
    g_rtos_debug.started = 1U;
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
