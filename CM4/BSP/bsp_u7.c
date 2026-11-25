#include "bsp_u7.h"
#include "stm32h7xx_hal.h"

//配置U7的PI6、PC15相位输入及PI9按键输入
SysResult_t BSP_U7_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    //PC15与LSE共用，分配时钟时不能改写引脚配置
    if ((RCC->BDCR & RCC_BDCR_LSEON) != 0U)
        return SYS_ERR_NOT_SUPPORTED;

    __HAL_RCC_GPIOI_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_9;
    HAL_GPIO_Init(GPIOI, &gpio);
    gpio.Pin = GPIO_PIN_15;
    HAL_GPIO_Init(GPIOC, &gpio);
    return SYS_OK;
}

//按原极性读取两路相位及低电平有效的按键
BspU7Sample_t BSP_U7_Read(void)
{
    BspU7Sample_t sample;

    sample.phase = (uint8_t)((HAL_GPIO_ReadPin(GPIOI, GPIO_PIN_6) == GPIO_PIN_SET ? 2U : 0U) |
        (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_15) == GPIO_PIN_SET ? 1U : 0U));
    sample.pressed = HAL_GPIO_ReadPin(GPIOI, GPIO_PIN_9) == GPIO_PIN_RESET;
    return sample;
}
