#ifndef CAMERA_TEST_HAL_H
#define CAMERA_TEST_HAL_H
#include <stdint.h>
#include <cmsis_compiler.h>
typedef enum {GPIO_PIN_RESET=0,GPIO_PIN_SET=1} GPIO_PinState;
typedef struct {uint32_t Pin,Mode,Pull,Speed,Alternate;} GPIO_InitTypeDef;
typedef struct {uint32_t PLL1_P_Frequency,PLL1_Q_Frequency,PLL1_R_Frequency;} PLL1_ClocksTypeDef;
typedef struct {volatile uint32_t CTRL,CYCCNT;} DWT_Type;
typedef struct {volatile uint32_t DEMCR;} Debug_Type;
typedef struct {volatile uint32_t PLLCFGR;} RCC_Type;
#define DWT ((DWT_Type *)0xE0001000U)
#define CoreDebug ((Debug_Type *)0xE000EDF0U)
#define RCC ((RCC_Type *)0x58024400U)
#define GPIOH ((void *)0x58021C00U)
#define GPIOB ((void *)0x58020400U)
#define GPIO_PIN_8 (1U<<8)
#define GPIO_PIN_9 (1U<<9)
#define GPIO_PIN_15 (1U<<15)
#define GPIO_MODE_OUTPUT_PP 1U
#define GPIO_MODE_OUTPUT_OD 0x11U
#define GPIO_NOPULL 0U
#define GPIO_SPEED_FREQ_LOW 0U
#define RCC_FLAG_PLLRDY 1U
#define RCC_PLLCFGR_DIVQ1EN 1U
#define RCC_MCO1 0U
#define RCC_MCO1SOURCE_PLL1QCLK 3U
#define RCC_MCODIV_10 10U
#define CoreDebug_DEMCR_TRCENA_Msk 0x1000000U
#define DWT_CTRL_CYCCNTENA_Msk 1U
#define RESET 0U
#define __HAL_RCC_GET_FLAG(flag) (1U)
#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOH_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOB_CLK_ENABLE() ((void)0)

extern uint32_t SystemCoreClock;
void SystemCoreClockUpdate(void);
void HAL_GPIO_Init(void *port,GPIO_InitTypeDef *gpio);
void HAL_GPIO_WritePin(void *port,uint16_t pins,GPIO_PinState state);
GPIO_PinState HAL_GPIO_ReadPin(void *port,uint16_t pin);
void HAL_RCCEx_GetPLL1ClockFreq(PLL1_ClocksTypeDef *clocks);
void HAL_RCC_MCOConfig(uint32_t mco,uint32_t source,uint32_t divisor);
void HAL_Delay(uint32_t ms);
#endif
