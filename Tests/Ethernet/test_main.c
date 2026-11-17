/* Isolated raw-frame bench firmware. NEVER part of production Application. */
#include "stm32h7xx_hal.h"
#include "eth_mac.h"
#include "eth_memory.h"
#include <string.h>
volatile uint32_t g_eth_test_command; /* 1 init, 2 start, 3 stop, 4 send once */
volatile uint32_t g_eth_test_continuous;
volatile int32_t g_eth_test_result;
volatile uint32_t g_eth_test_rx_count, g_eth_test_tx_done, g_eth_test_sequence;
volatile uint32_t g_eth_test_last_ticket;
volatile eth_statistics g_eth_test_statistics;
volatile uint32_t g_eth_test_last_rx_length;
uint8_t g_eth_test_last_rx[ETH_BUFFER_SIZE];
static const uint8_t test_mac[6] = {0x02U, 0x47U, 0x47U, 0U, 0U, 1U};
static uint8_t frame[60];
void Error_Handler(void);
void SystemClock_Config(void);
int main(void)
{
	uint32_t last = 0U, service_tick = 0U;
	HAL_MPU_Disable();
	eth_memory_configure();
	HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
	SCB_EnableICache();
	SCB_EnableDCache();
	HAL_Init();
	SystemClock_Config();
	memset(frame, 0xA5, sizeof(frame));
	memset(frame, 0xFF, 6U);
	memcpy(frame + 6, test_mac, 6U);
	frame[12] = 0x88U;
	frame[13] = 0xB5U;
	for (;;)
	{
		uint32_t command = g_eth_test_command, ticket;
		eth_statistics snapshot;
		size_t length;
		eth_result result;
		g_eth_test_command = 0U;
		if (command == 1U)
			g_eth_test_result = eth_mac_init(test_mac);
		else if (command == 2U)
			g_eth_test_result = eth_mac_start();
		else if (command == 3U)
		{
			g_eth_test_continuous = 0U;
			g_eth_test_result = eth_mac_stop();
		}
		if (command == 4U || (g_eth_test_continuous && HAL_GetTick() != last))
		{
			uint32_t n = ++g_eth_test_sequence;
			last = HAL_GetTick();
			frame[14] = (uint8_t)(n >> 24);
			frame[15] = (uint8_t)(n >> 16);
			frame[16] = (uint8_t)(n >> 8);
			frame[17] = (uint8_t)n;
			result = eth_mac_tx_submit(frame, sizeof(frame), &ticket);
			g_eth_test_result = result;
			if (result == ETH_OK)
				g_eth_test_last_ticket = ticket;
		}
		result = eth_mac_tx_reclaim(&ticket);
		if (result == ETH_OK)
			++g_eth_test_tx_done;
		result = eth_mac_rx_poll(g_eth_test_last_rx, sizeof(g_eth_test_last_rx), &length);
		if (result == ETH_OK)
		{
			++g_eth_test_rx_count;
			g_eth_test_last_rx_length = (uint32_t)length;
		}
		if (eth_mac_get_statistics(&snapshot) == ETH_OK)
		{
			g_eth_test_statistics = snapshot;
			if ((snapshot.state == ETH_MAC_RUNNING || snapshot.state == ETH_MAC_FAULT) &&
				(uint32_t)(HAL_GetTick() - service_tick) >= 10U)
			{
				service_tick = HAL_GetTick();
				result = eth_mac_service();
				if (result != ETH_OK)
					g_eth_test_result = result;
			}
		}
	}
}
void Error_Handler(void)
{
	g_eth_test_result = -100;
	__disable_irq();
	for (;;)
	{
	}
}

void SystemClock_Config(void)
{
	RCC_OscInitTypeDef RCC_OscInitStruct = {0};
	RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

	/** Supply configuration update enable
  */
	HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

	/** Configure the main internal regulator output voltage
  */
	__HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

	while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY))
	{
	}

	/** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
	RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
	RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
	RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
	RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
	RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
	RCC_OscInitStruct.PLL.PLLM = 4;
	RCC_OscInitStruct.PLL.PLLN = 60;
	RCC_OscInitStruct.PLL.PLLP = 2;
	RCC_OscInitStruct.PLL.PLLQ = 4;
	RCC_OscInitStruct.PLL.PLLR = 2;
	RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
	RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
	RCC_OscInitStruct.PLL.PLLFRACN = 0;
	if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
	{
		Error_Handler();
	}

	/** Initializes the CPU, AHB and APB buses clocks
  */
	RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 |
								  RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_D3PCLK1 |
								  RCC_CLOCKTYPE_D1PCLK1;
	RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
	RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
	RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
	RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
	RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
	RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
	RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

	if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
	{
		Error_Handler();
	}
}
