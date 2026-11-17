//STM32H747 原理图相关的时钟、RMII GPIO 和 PHY 硬件复位
#include "eth_board.h"
#ifdef ETH_MAC_HOST_TEST
#include "fake_hal.h"
#else
#include "stm32h7xx_hal.h"
#endif

//内部功能函数声明
static void EthBoard_ConfigurePins(void);

//对外接口实现

//准备板级资源，不配置描述符或启动 MAC/DMA
void EthBoard_Prepare(void)
{
	__HAL_RCC_SYSCFG_CLK_ENABLE();
	HAL_SYSCFG_ETHInterfaceSelect(SYSCFG_ETH_RMII);
	(void)SYSCFG->PMCR;
	__HAL_RCC_ETH1MAC_CLK_ENABLE();
	__HAL_RCC_ETH1TX_CLK_ENABLE();
	__HAL_RCC_ETH1RX_CLK_ENABLE();
	EthBoard_ConfigurePins();
}

//内部功能函数实现

//按原理图配置 RMII 引脚并复位 PHY
static void EthBoard_ConfigurePins(void)
{
	GPIO_InitTypeDef gpio = {0};

	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOC_CLK_ENABLE();
	__HAL_RCC_GPIOG_CLK_ENABLE();
	//PG9 控制 PHY 复位，RMII 输入和 strap 稳定前保持低电平
	HAL_GPIO_WritePin(GPIOG, GPIO_PIN_9, GPIO_PIN_RESET);
	gpio.Pin = GPIO_PIN_9;
	gpio.Mode = GPIO_MODE_OUTPUT_PP;
	gpio.Pull = GPIO_NOPULL;
	gpio.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOG, &gpio);
	gpio.Mode = GPIO_MODE_AF_PP;
	gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
	gpio.Alternate = GPIO_AF11_ETH;
	gpio.Pin = GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_7;
	HAL_GPIO_Init(GPIOA, &gpio);
	gpio.Pin = GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5;
	HAL_GPIO_Init(GPIOC, &gpio);
	gpio.Pin = GPIO_PIN_11 | GPIO_PIN_13 | GPIO_PIN_14;
	HAL_GPIO_Init(GPIOG, &gpio);
	HAL_Delay(2U);
	HAL_GPIO_WritePin(GPIOG, GPIO_PIN_9, GPIO_PIN_SET);
	HAL_Delay(10U);
}
