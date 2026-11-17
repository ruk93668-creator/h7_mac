//STM32H747 原理图相关的时钟、RMII GPIO 和 PHY 硬件复位
#include "eth_board.h"
#ifdef ETH_MAC_HOST_TEST
#include "fake_hal.h"
#else
#include "stm32h7xx_hal.h"
#endif

//按原理图配置 RMII 引脚并复位 PHY
static void pins(void)
{
	GPIO_InitTypeDef g = {0};
	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOC_CLK_ENABLE();
	__HAL_RCC_GPIOG_CLK_ENABLE();
	//PG9 控制 PHY 复位，RMII 输入和 strap 稳定前保持低电平
	HAL_GPIO_WritePin(GPIOG, GPIO_PIN_9, GPIO_PIN_RESET);
	g.Pin = GPIO_PIN_9;
	g.Mode = GPIO_MODE_OUTPUT_PP;
	g.Pull = GPIO_NOPULL;
	g.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOG, &g);
	g.Mode = GPIO_MODE_AF_PP;
	g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
	g.Alternate = GPIO_AF11_ETH;
	g.Pin = GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_7;
	HAL_GPIO_Init(GPIOA, &g);
	g.Pin = GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5;
	HAL_GPIO_Init(GPIOC, &g);
	g.Pin = GPIO_PIN_11 | GPIO_PIN_13 | GPIO_PIN_14;
	HAL_GPIO_Init(GPIOG, &g);
	HAL_Delay(2U);
	HAL_GPIO_WritePin(GPIOG, GPIO_PIN_9, GPIO_PIN_SET);
	HAL_Delay(10U);
}

//准备板级资源，不配置描述符或启动 MAC/DMA
void EthBoard_Prepare(void)
{
	__HAL_RCC_SYSCFG_CLK_ENABLE();
	HAL_SYSCFG_ETHInterfaceSelect(SYSCFG_ETH_RMII);
	(void)SYSCFG->PMCR;
	__HAL_RCC_ETH1MAC_CLK_ENABLE();
	__HAL_RCC_ETH1TX_CLK_ENABLE();
	__HAL_RCC_ETH1RX_CLK_ENABLE();
	pins();
}
