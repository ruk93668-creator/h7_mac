#include "platform_rtos.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stm32h7xx.h"
#include <stddef.h>
#include <stdbool.h>

//保存Idle任务的静态控制信息
static StaticTask_t s_idle_tcb;

//保存Idle任务的运行栈，按Port要求对齐
static StackType_t s_idle_stack[configMINIMAL_STACK_SIZE] __attribute__((aligned(8)));

//记录是否尝试过启动调度器，防止重复启动
static bool s_start_attempted;

//保存内核Tick、空闲运行和故障调试信息
volatile PlatformRtosDebug_t g_rtos_debug;

//启动FreeRTOS调度器，不创建业务任务
SysResult_t Platform_RtosStart(void)
{

    //只允许非中断、全局中断未关闭且已有业务任务时启动一次
    if (__get_IPSR() != 0U || __get_PRIMASK() != 0U ||
        s_start_attempted || xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED ||
        uxTaskGetNumberOfTasks() == 0U)

        return SYS_ERR_INVALID_STATE;

    //核对芯片优先级位数与FreeRTOS配置一致
    configASSERT(__NVIC_PRIO_BITS == configPRIO_BITS);

    //核对中断优先级分组与当前Port约定一致
    configASSERT(NVIC_GetPriorityGrouping() == 3U);

    //锁定启动入口；调试started表示进入启动过程，不代表任务已运行
    s_start_attempted = true;
    g_rtos_debug.started = 1U;

    //由内核创建Idle并启动首个任务，正常调度后不再返回
    //创建任务后可能保留BASEPRI屏蔽，由首任务启动流程恢复
    vTaskStartScheduler();

    //调度器异常返回时交给main处理，不能退回Legacy轮询
    return SYS_ERR_INVALID_STATE;
}

//保存处理器故障寄存器
void Platform_RtosRecordFault(void)
{

    //保留SCB故障原因，供停机后查看，不在这里尝试恢复
    g_rtos_debug.cfsr = SCB->CFSR;
    g_rtos_debug.hfsr = SCB->HFSR;
}

//记录断言位置并停机
void Platform_RtosAssert(const char *file, uint32_t line)
{

    //禁止继续运行，避免错误状态下再次访问业务资源
    __disable_irq();

    //保存触发位置和故障寄存器，随后停在原地供调试
    g_rtos_debug.fault_file = file;
    g_rtos_debug.fault_line = line;
    Platform_RtosRecordFault();
    while (1) { }
}

//向内核提供Idle任务的静态内存
void vApplicationGetIdleTaskMemory(StaticTask_t **tcb, StackType_t **stack, uint32_t *size)
{

    //内核通过输出参数取得静态TCB、栈地址和栈元素数量
    *tcb = &s_idle_tcb;
    *stack = s_idle_stack;
    *size = configMINIMAL_STACK_SIZE;
}

//记录内核Tick次数
void vApplicationTickHook(void)
{

    //Tick Hook处于中断上下文，只计数，不阻塞或处理业务
    g_rtos_debug.tick_count++;
}

//记录空闲次数并等待中断
void vApplicationIdleHook(void)
{

    //没有其他就绪业务时由Idle记录空闲机会
    g_rtos_debug.idle_iterations++;

    //进入等待中断；USB的Sleep时钟配置仍由BSP负责
    __WFI();
}

//记录任务栈溢出并停机
void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;

    //由内核报告栈溢出，以任务名记录故障并停止运行
    Platform_RtosAssert(name, __LINE__);
}
