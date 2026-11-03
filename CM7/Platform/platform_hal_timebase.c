#include "stm32h7xx_hal.h"

//HAL Tick高于内核BASEPRI屏蔽阈值，ISR内禁止调用RTOS API
#define HAL_TIMEBASE_IRQ_PRIORITY 4U

//根据APB1分频和TIMPRE计算TIM6内核时钟
static uint32_t HalTimebase_TimerClock(uint32_t hclk, uint32_t pclk, uint32_t timpre);

HAL_StatusTypeDef HAL_InitTick(uint32_t priority)
{
    uint32_t clock;
    if (priority >= (1UL << __NVIC_PRIO_BITS))
        return HAL_ERROR;

    clock = HalTimebase_TimerClock(HAL_RCC_GetHCLKFreq(), HAL_RCC_GetPCLK1Freq(),
        RCC->CFGR & RCC_CFGR_TIMPRE);
    if (clock < 1000000U || clock % 1000000U != 0U || clock / 1000000U > 65536U)
        return HAL_ERROR;

    __HAL_RCC_TIM6_CLK_ENABLE();
    TIM6->CR1 = 0U;
    TIM6->DIER = 0U;
    TIM6->PSC = clock / 1000000U - 1U;
    TIM6->ARR = 999U;
    TIM6->CNT = 0U;
    TIM6->EGR = TIM_EGR_UG;
    TIM6->SR = 0U;
    HAL_NVIC_ClearPendingIRQ(TIM6_DAC_IRQn);
    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, HAL_TIMEBASE_IRQ_PRIORITY, 0U);
    uwTickPrio = HAL_TIMEBASE_IRQ_PRIORITY;
    TIM6->DIER = TIM_DIER_UIE;
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
    TIM6->CR1 = TIM_CR1_CEN;
    return HAL_OK;
}

void HAL_SuspendTick(void)
{
    TIM6->DIER &= ~TIM_DIER_UIE;
}

void HAL_ResumeTick(void)
{
    TIM6->DIER |= TIM_DIER_UIE;
}

void TIM6_DAC_IRQHandler(void)
{
    if ((TIM6->SR & TIM_SR_UIF) != 0U && (TIM6->DIER & TIM_DIER_UIE) != 0U)
    {
        TIM6->SR = ~TIM_SR_UIF;
        HAL_IncTick();
    }
}

//内部功能函数实现
static uint32_t HalTimebase_TimerClock(uint32_t hclk, uint32_t pclk, uint32_t timpre)
{
    if (pclk == 0U || hclk < pclk)
        return 0U;
    if (timpre == 0U)
        return hclk == pclk ? pclk : pclk * 2U;
    return hclk / pclk <= 4U ? hclk : pclk * 4U;
}
