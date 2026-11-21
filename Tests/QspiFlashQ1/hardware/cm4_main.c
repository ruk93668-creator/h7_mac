#include "main.h"

//配套测试 CM4 只执行标准启动握手，随后停驻，不运行生产 Application/IPC/GPIO。
int main(void)
{
    __HAL_RCC_HSEM_CLK_ENABLE();
    HAL_HSEM_ActivateNotification(__HAL_HSEM_SEMID_TO_MASK(0U));
    HAL_PWREx_ClearPendingEvent();
    HAL_PWREx_EnterSTOPMode(PWR_MAINREGULATOR_ON, PWR_STOPENTRY_WFE, PWR_D2_DOMAIN);
    __HAL_HSEM_CLEAR_FLAG(__HAL_HSEM_SEMID_TO_MASK(0U));
    for (;;)
        __WFI();
}

void Error_Handler(void)
{
    for (;;)
        __WFI();
}
