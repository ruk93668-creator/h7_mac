#include "main.h"
#include "h2.h"
#include "platform_rtos.h"
#include "FreeRTOS.h"
#include "task.h"

#define H2_TASK_WORDS 768U
static StaticTask_t s_test_tcb;
static StackType_t s_test_stack[H2_TASK_WORDS] __attribute__((aligned(8)));

void SystemClock_Config(void);

static void H2_Task(void *argument)
{
    (void)argument;
    for (;;)
    {
        H2_Step();
        vTaskDelay(pdMS_TO_TICKS(10U));
    }
}

void Error_Handler(void)
{
    H2_BootFailure();
    for (;;)
        __WFI();
}

int main(void)
{
    int32_t timeout = 0xFFFFFF;
    H2_Boot();
    HAL_RCCEx_EnableBootCore(RCC_BOOT_C2);
    while (__HAL_RCC_GET_FLAG(RCC_FLAG_D2CKRDY) != RESET && timeout-- > 0)
    {
    }
    if (timeout < 0 || HAL_Init() != HAL_OK)
        Error_Handler();
    SystemClock_Config();
    __HAL_RCC_HSEM_CLK_ENABLE();
    if (HAL_HSEM_FastTake(0U) != HAL_OK)
        Error_Handler();
    HAL_HSEM_Release(0U, 0U);
    timeout = 0xFFFFFF;
    while (__HAL_RCC_GET_FLAG(RCC_FLAG_D2CKRDY) == RESET && timeout-- > 0)
    {
    }
    if (timeout < 0 || xTaskCreateStatic(H2_Task, "qspi_h2", H2_TASK_WORDS, NULL, 1U,
        s_test_stack, &s_test_tcb) == NULL)
        Error_Handler();
    (void)Platform_RtosStart();
    Error_Handler();
    return 0;
}
