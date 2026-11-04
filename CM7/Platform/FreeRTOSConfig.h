#ifndef CM7_FREERTOS_CONFIG_H
#define CM7_FREERTOS_CONFIG_H

#include <stdint.h>

//由系统时钟配置维护，供内核计算Tick时钟
extern uint32_t SystemCoreClock;

//断言只记录并停机，不在异常中调用业务模块
void Platform_RtosAssert(const char *file, uint32_t line);
#define configASSERT(x) do { if (!(x)) Platform_RtosAssert(__FILE__, __LINE__); } while (0)

//M7使用1ms内核Tick，HAL毫秒时基独立运行
#define configCPU_CLOCK_HZ                   SystemCoreClock
#define configTICK_RATE_HZ                   1000U
#define configUSE_PREEMPTION                 1
#define configUSE_TIME_SLICING               1
#define configUSE_TICKLESS_IDLE              0
#define configMAX_PRIORITIES                 5U
#define configMINIMAL_STACK_SIZE             256U
#define configMAX_TASK_NAME_LEN              16U
#define configUSE_16_BIT_TICKS               0
#define configIDLE_SHOULD_YIELD              1

//任务和空闲任务均静态分配，不引入内核堆
#define configSUPPORT_STATIC_ALLOCATION      1
#define configSUPPORT_DYNAMIC_ALLOCATION     0
#define configUSE_TIMERS                     0
#define configUSE_MUTEXES                    0
#define configUSE_RECURSIVE_MUTEXES          0
#define configUSE_COUNTING_SEMAPHORES        0
#define configQUEUE_REGISTRY_SIZE            0
#define configUSE_TASK_NOTIFICATIONS         1
#define configUSE_IDLE_HOOK                  1
#define configUSE_TICK_HOOK                  1
#define configCHECK_FOR_STACK_OVERFLOW       2
#define configUSE_MALLOC_FAILED_HOOK         0
#define configUSE_TRACE_FACILITY             0
#define configGENERATE_RUN_TIME_STATS        0
#define INCLUDE_xTaskGetCurrentTaskHandle    1
#define INCLUDE_xTaskGetIdleTaskHandle       1
#define INCLUDE_vTaskDelay                   1
#define INCLUDE_vTaskSuspend                 1
#define INCLUDE_xTaskGetSchedulerState       1
#define INCLUDE_uxTaskGetStackHighWaterMark   1

//H747实现4个优先级位，数值越小，中断优先级越高
#define configPRIO_BITS                      4U
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15U
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5U
#define configKERNEL_INTERRUPT_PRIORITY      (15 << 4)
#define configMAX_SYSCALL_INTERRUPT_PRIORITY  (5 << 4)

//异常向量由官方ARMCC5 Port唯一实现
#define vPortSVCHandler                      SVC_Handler
#define xPortPendSVHandler                   PendSV_Handler

#endif
