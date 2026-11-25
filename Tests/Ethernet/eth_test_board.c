#include "stm32h7xx_hal.h"
#include "eth_memory.h"
#include "eth_test_board.h"
#include "lwip/sys.h"

static void EthBench_Mpu(void (*extra_mpu)(void))
{
	MPU_Region_InitTypeDef cache_region = {0};

	HAL_MPU_Disable();
	eth_memory_configure();
	//沿用 E3.5 Region 5，放置仅由 CPU 访问的 lwIP 池和帧副本
	cache_region.Enable = MPU_REGION_ENABLE;
	cache_region.Number = MPU_REGION_NUMBER5;
	cache_region.BaseAddress = 0x24010000U;
	cache_region.Size = MPU_REGION_SIZE_64KB;
	cache_region.AccessPermission = MPU_REGION_FULL_ACCESS;
	cache_region.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
	cache_region.TypeExtField = MPU_TEX_LEVEL1;
	cache_region.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
	cache_region.IsCacheable = MPU_ACCESS_CACHEABLE;
	cache_region.IsBufferable = MPU_ACCESS_BUFFERABLE;
	HAL_MPU_ConfigRegion(&cache_region);
	if (extra_mpu)
		extra_mpu();
	HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

static int EthBench_Clock(void)
{
	RCC_OscInitTypeDef osc = {0};
	RCC_ClkInitTypeDef clock = {0};

	if (HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY) != HAL_OK)
		return 0;
	__HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);
	while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY))
	{
	}
	osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
	osc.HSIState = RCC_HSI_DIV1;
	osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
	osc.PLL.PLLState = RCC_PLL_ON;
	osc.PLL.PLLSource = RCC_PLLSOURCE_HSI;
	osc.PLL.PLLM = 4U;
	osc.PLL.PLLN = 60U;
	osc.PLL.PLLP = 2U;
	osc.PLL.PLLQ = 4U;
	osc.PLL.PLLR = 2U;
	osc.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
	osc.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
	if (HAL_RCC_OscConfig(&osc) != HAL_OK)
		return 0;

	clock.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 |
		RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_D3PCLK1 | RCC_CLOCKTYPE_D1PCLK1;
	clock.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
	clock.SYSCLKDivider = RCC_SYSCLK_DIV1;
	clock.AHBCLKDivider = RCC_HCLK_DIV2;
	clock.APB3CLKDivider = RCC_APB3_DIV2;
	clock.APB1CLKDivider = RCC_APB1_DIV2;
	clock.APB2CLKDivider = RCC_APB2_DIV2;
	clock.APB4CLKDivider = RCC_APB4_DIV2;
	return HAL_RCC_ClockConfig(&clock, FLASH_LATENCY_4) == HAL_OK;
}

void SysTick_Handler(void)
{
	HAL_IncTick();
}

int eth_test_board_init(void)
{
	return eth_test_board_init_with_mpu(0);
}

int eth_test_board_init_with_mpu(void (*extra_mpu)(void))
{
	if (HAL_Init() != HAL_OK || !EthBench_Clock())
		return 0;
	EthBench_Mpu(extra_mpu);
	SCB_EnableICache();
	SCB_EnableDCache();
	return 1;
}
