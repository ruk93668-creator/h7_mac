#ifndef OLED_MONITOR_HAL_STUB_H
#define OLED_MONITOR_HAL_STUB_H

#include <stdint.h>

typedef struct { uint32_t BDCR; } OledTestRcc_t;
typedef struct { uint32_t Pin, Mode, Pull; } GPIO_InitTypeDef;
typedef enum { GPIO_PIN_RESET, GPIO_PIN_SET } GPIO_PinState;

#define RCC_BDCR_LSEON 1U
#define GPIO_PIN_6 (1U << 6)
#define GPIO_PIN_9 (1U << 9)
#define GPIO_PIN_15 (1U << 15)
#define GPIO_MODE_INPUT 0U
#define GPIO_PULLUP 1U
#define GPIOI 1U
#define GPIOC 2U

extern OledTestRcc_t g_test_rcc;
#define RCC (&g_test_rcc)

void Test_EnableGpio(uint32_t port);
#define __HAL_RCC_GPIOI_CLK_ENABLE() Test_EnableGpio(GPIOI)
#define __HAL_RCC_GPIOC_CLK_ENABLE() Test_EnableGpio(GPIOC)

void HAL_GPIO_Init(uint32_t port, const GPIO_InitTypeDef *gpio);
GPIO_PinState HAL_GPIO_ReadPin(uint32_t port, uint32_t pin);

#endif
