#ifndef CAMERA_POWER_TEST_HAL
#define CAMERA_POWER_TEST_HAL
#include <stdint.h>
typedef struct { uint32_t Pin,Mode,Pull,Speed; } GPIO_InitTypeDef;
typedef uint32_t GPIO_PinState;
typedef struct { uint32_t PLL1_Q_Frequency; } PLL1_ClocksTypeDef;
typedef struct { uint32_t PLLCFGR; } RCC_TypeDef;
typedef struct { volatile uint32_t CTRL,CYCCNT; } DWT_Type;
typedef struct { uint32_t DEMCR; } CoreDebug_Type;
extern RCC_TypeDef mock_rcc;
extern DWT_Type mock_dwt;
extern CoreDebug_Type mock_debug;
extern uint32_t mock_pll_ready,mock_frequency,mock_dwt_runs,SystemCoreClock;
#define RCC (&mock_rcc)
#define DWT (&mock_dwt)
#define CoreDebug (&mock_debug)
#define GPIOA 1U
#define GPIOB 2U
#define GPIOH 3U
#define GPIO_PIN_8 (1U<<8)
#define GPIO_PIN_9 (1U<<9)
#define GPIO_PIN_13 (1U<<13)
#define GPIO_PIN_15 (1U<<15)
#define GPIO_PIN_RESET 0U
#define GPIO_PIN_SET 1U
#define GPIO_MODE_OUTPUT_PP 1U
#define GPIO_MODE_OUTPUT_OD 2U
#define GPIO_MODE_ANALOG 3U
#define GPIO_NOPULL 0U
#define GPIO_PULLDOWN 2U
#define GPIO_SPEED_FREQ_LOW 0U
#define RCC_FLAG_PLLRDY 1U
#define RCC_PLLCFGR_DIVQ1EN 1U
#define RCC_MCO1 1U
#define RCC_MCO1SOURCE_PLL1QCLK 2U
#define RCC_MCODIV_10 10U
#define RESET 0U
#define CoreDebug_DEMCR_TRCENA_Msk 1U
#define DWT_CTRL_CYCCNTENA_Msk 1U
#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOB_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOH_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GET_FLAG(x) mock_pll_ready
#define __NOP() ((void)(DWT->CYCCNT += mock_dwt_runs))
void HAL_GPIO_WritePin(uint32_t port,uint32_t pin,GPIO_PinState level);
void HAL_GPIO_Init(uint32_t port,GPIO_InitTypeDef *gpio);
GPIO_PinState HAL_GPIO_ReadPin(uint32_t port,uint32_t pin);
void HAL_RCCEx_GetPLL1ClockFreq(PLL1_ClocksTypeDef *clocks);
void HAL_RCC_MCOConfig(uint32_t mco,uint32_t src,uint32_t div);
void HAL_Delay(uint32_t ms);
void SystemCoreClockUpdate(void);
#endif
