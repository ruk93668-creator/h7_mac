//E3.5 独立验收入口；不使用 Application / FreeRTOS，不写 DMA 描述符
#include "stm32h7xx_hal.h"
#include "eth_mac.h"
#include "eth_memory.h"
#include "e35_frame.h"
#include <string.h>

typedef struct
{
	uint32_t active;
	uint32_t start_tick;
	uint32_t end_tick;
	uint32_t heartbeat;
	uint32_t poll_calls;
	uint32_t raw_rx;
	uint32_t valid_rx;
	uint32_t ignored;
	uint32_t corrupt;
	uint32_t gaps;
	uint32_t late_or_duplicate;
	uint32_t next_sequence;
	uint32_t tx_accepted;
	uint32_t tx_completed;
	uint32_t tx_busy;
	uint32_t errors;
	int32_t first_error;
	int32_t service_result;
	uint32_t ccr;
	uint32_t memory_valid;
	uint32_t dma_status;
	uint32_t tx_own_mask;
	uint32_t rx_own_mask;
	uint32_t mac_config;
	uint32_t vlan_config;
	uint32_t caller_tx_address;
	uint32_t caller_rx_address;
	uint32_t cache_pressure_passes;
} EthValidationMetrics_t;

//只在 active=0 时修改配置；命令 7 锁存。session 每次测试必须更换
volatile uint32_t g_e35_command;
volatile int32_t g_e35_result;
volatile uint32_t g_e35_session = 1U;
volatile uint32_t g_e35_length = 60U;
volatile uint32_t g_e35_vlan;
volatile uint32_t g_e35_poll_enabled = 1U;
volatile uint32_t g_e35_tx_enabled;
volatile uint32_t g_e35_interval_ms = 10U;
volatile uint32_t g_e35_tx_duration_ms = 600000U;
volatile uint32_t g_e35_tx_elapsed_ms;
volatile uint32_t g_e35_restarts;
volatile int32_t g_e35_restart_stop[100];
volatile int32_t g_e35_restart_start[100];
volatile int32_t g_e35_burst[4];
volatile EthValidationMetrics_t g_e35_metrics;
volatile EthStatistics_t g_e35_statistics;
volatile EthLinkState_t g_e35_link;

static const uint8_t s_mac[6] = {2U, 0x47U, 0x47U, 0U, 0U, 1U};
//仅验收目标的独立 AXI SRAM 缓存区，和生产 DMA 非缓存窗口完全分开
static uint8_t s_tx[ETH_BUFFER_SIZE] __attribute__((section(".e35_cache"), aligned(32)));
static uint8_t s_rx[ETH_BUFFER_SIZE] __attribute__((section(".e35_cache"), aligned(32)));
static volatile uint32_t s_cache_pressure[8192] __attribute__((section(".e35_cache"), aligned(32)));
static EthValidationFrame_t s_config;
static uint32_t s_tx_start;
static uint32_t s_last_tx;
static uint32_t s_tx_was_enabled;
static uint32_t s_restart_active;

void SystemClock_Config(void);
void Error_Handler(void);
static void EthValidation_Record(EthResult_t result);
static EthResult_t EthValidation_Send(void);
static void EthValidation_Command(uint32_t command);
static void EthValidation_Snapshot(void);

int main(void)
{
	uint32_t service_tick = 0U;
	MPU_Region_InitTypeDef cache_region = {0};
	HAL_MPU_Disable();
	eth_memory_configure();
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
	HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
	SCB_EnableICache();
	SCB_EnableDCache();
	HAL_Init();
	SystemClock_Config();
	for (;
		 ;
		 )
	{
		uint32_t command = g_e35_command;
		uint32_t ticket;
		size_t length;
		EthResult_t result;
		++g_e35_metrics.heartbeat;
		g_e35_command = 0U;
		if (command != 0U)
			EthValidation_Command(command);
		if (s_restart_active)
		{
			uint32_t index = g_e35_restarts;
			result = eth_mac_stop();
			g_e35_restart_stop[index] = result;
			if (result == ETH_OK)
			{
				result = eth_mac_start();
				g_e35_restart_start[index] = result;
			}
			g_e35_result = result;
			EthValidation_Record(result);
			if (result != ETH_OK || ++g_e35_restarts == 100U)
				s_restart_active = 0U;
		}
		if (g_e35_tx_enabled && g_e35_metrics.active)
		{
			if (!s_tx_was_enabled)
			{
				s_tx_start = HAL_GetTick();
				s_last_tx = s_tx_start;
			}
			s_tx_was_enabled = 1U;
			g_e35_tx_elapsed_ms = HAL_GetTick() - s_tx_start;
			if (g_e35_tx_elapsed_ms >= g_e35_tx_duration_ms)
				g_e35_tx_enabled = 0U;
			else if ((uint32_t)(HAL_GetTick() - s_last_tx) >= g_e35_interval_ms)
			{
				s_last_tx = HAL_GetTick();
				EthValidation_Send();
			}
		}
		else
			s_tx_was_enabled = 0U;
		result = eth_mac_tx_reclaim(&ticket);
		if (result == ETH_OK && g_e35_metrics.active)
			++g_e35_metrics.tx_completed;
		else if (result != ETH_EMPTY && result != ETH_STATE)
			EthValidation_Record(result);
		if (g_e35_poll_enabled)
		{
			EthValidationFrame_t decoded;
			int checked;
			++g_e35_metrics.poll_calls;
			result = eth_mac_rx_poll(s_rx, sizeof(s_rx), &length);
			if (result == ETH_OK)
			{
				++g_e35_metrics.raw_rx;
				checked = EthValidation_Check(s_rx, length, s_config.session, 0, &decoded);
				if (!g_e35_metrics.active || checked == 0)
					++g_e35_metrics.ignored;
				else if (checked < 0 || decoded.length != s_config.length || decoded.vlan != s_config.vlan)
					++g_e35_metrics.corrupt;
				else
				{
					++g_e35_metrics.valid_rx;
					if (decoded.sequence < g_e35_metrics.next_sequence)
						++g_e35_metrics.late_or_duplicate;
					else
					{
						g_e35_metrics.gaps += decoded.sequence - g_e35_metrics.next_sequence;
						g_e35_metrics.next_sequence = decoded.sequence + 1U;
					}
				}
			}
			else if (result != ETH_EMPTY && result != ETH_STATE)
				EthValidation_Record(result);
		}
		if ((uint32_t)(HAL_GetTick() - service_tick) >= 10U)
		{
			uint32_t index;
			service_tick = HAL_GetTick();
			//写遍 32 KiB 缓存数据，再读回检查；持续制造真实 DCache 压力
			for (index = 0U;
				 index < 8192U;
				 ++index)
				s_cache_pressure[index] = index ^ service_tick;
			for (index = 0U;
				 index < 8192U;
				 ++index)
				if (s_cache_pressure[index] != (index ^ service_tick))
					EthValidation_Record(ETH_MEMORY);
			++g_e35_metrics.cache_pressure_passes;
			EthValidation_Snapshot();
			if (g_e35_statistics.state == ETH_MAC_RUNNING || g_e35_statistics.state == ETH_MAC_FAULT)
			{
				result = eth_mac_service();
				g_e35_metrics.service_result = result;
				EthValidation_Record(result);
			}
		}
	}
}

static void EthValidation_Record(EthResult_t result)
{
	if (result < ETH_OK && g_e35_metrics.active)
	{
		++g_e35_metrics.errors;
		if (g_e35_metrics.first_error == 0)
			g_e35_metrics.first_error = result;
	}
}

static EthResult_t EthValidation_Send(void)
{
	uint32_t ticket;
	EthResult_t result;
	s_config.sequence = g_e35_metrics.tx_accepted + 1U;
	if (!EthValidation_Build(s_tx, &s_config, 1))
		return ETH_ARGUMENT;
	result = eth_mac_tx_submit(s_tx, s_config.length, &ticket);
	if (result == ETH_OK)
	{
		++g_e35_metrics.tx_accepted;
		//污染调用者原始缓冲区，验证驱动确实同步复制，不能污染 DMA 内部副本
		memset(s_tx, 0xCC, sizeof(s_tx));
	}
	else if (result == ETH_BUSY)
		++g_e35_metrics.tx_busy;
	EthValidation_Record(result);
	return result;
}

static void EthValidation_Command(uint32_t command)
{
	uint32_t index;
	EthStatistics_t statistics;
	EthResult_t result = ETH_ARGUMENT;
	if (command == 1U)
		result = eth_mac_init(s_mac);
	else if (command == 2U)
		result = eth_mac_start();
	else if (command == 3U)
	{
		g_e35_tx_enabled = 0U;
		result = eth_mac_stop();
	}
	else if (command == 7U && !g_e35_metrics.active)
	{
		s_config.session = g_e35_session;
		s_config.sequence = 1U;
		s_config.length = (uint16_t)g_e35_length;
		s_config.vlan = g_e35_vlan != 0U;
		if (g_e35_length <= 1522U && EthValidation_Build(s_tx, &s_config, 1) &&
			eth_mac_get_statistics(&statistics) == ETH_OK && statistics.state == ETH_MAC_RUNNING)
		{
			g_e35_tx_enabled = 0U;
			s_tx_was_enabled = 0U;
			memset((void *)&g_e35_metrics, 0, sizeof(g_e35_metrics));
			g_e35_metrics.next_sequence = 1U;
			g_e35_metrics.start_tick = HAL_GetTick();
			g_e35_metrics.active = 1U;
			result = ETH_OK;
		}
	}
	else if (command == 8U)
	{
		g_e35_tx_enabled = 0U;
		g_e35_metrics.end_tick = HAL_GetTick();
		g_e35_metrics.active = 0U;
		result = ETH_OK;
	}
	else if (command == 9U || command == 10U)
	{
		g_e35_poll_enabled = command == 10U;
		result = ETH_OK;
	}
	else if (command == 11U && !g_e35_metrics.active)
	{
		g_e35_tx_enabled = 0U;
		g_e35_restarts = 0U;
		for (index = 0U;
			 index < 100U;
			 ++index)
		{
			g_e35_restart_stop[index] = 99;
			g_e35_restart_start[index] = 99;
		}
		s_restart_active = 1U;
		result = ETH_OK;
	}
	else if (command == 12U && g_e35_metrics.active && !g_e35_tx_enabled)
	{
		//调用四次提交且中途不 reclaim；空环容量 3，预期 0/0/0/BUSY
		for (index = 0U;
			 index < 4U;
			 ++index)
			g_e35_burst[index] = EthValidation_Send();
		result = ETH_OK;
	}
	else if (command == 4U && g_e35_metrics.active)
		result = EthValidation_Send();
	g_e35_result = result;
	EthValidation_Record(result);
	EthValidation_Snapshot();
}

static void EthValidation_Snapshot(void)
{
	EthStatistics_t statistics;
	EthLinkState_t link;
	uint32_t index;
	uint32_t tx_mask = 0U;
	uint32_t rx_mask = 0U;
	if (eth_mac_get_statistics(&statistics) == ETH_OK)
		g_e35_statistics = statistics;
	if (eth_mac_get_link(&link) == ETH_OK)
		g_e35_link = link;
	g_e35_metrics.ccr = SCB->CCR;
	g_e35_metrics.memory_valid = (uint32_t)eth_memory_valid();
	g_e35_metrics.dma_status = ETH->DMACSR;
	g_e35_metrics.mac_config = ETH->MACCR;
	g_e35_metrics.vlan_config = ETH->MACVTR;
	g_e35_metrics.caller_tx_address = (uint32_t)(uintptr_t)s_tx;
	g_e35_metrics.caller_rx_address = (uint32_t)(uintptr_t)s_rx;
	for (index = 0U;
		 index < ETH_RING_SIZE;
		 ++index)
	{
		if (g_eth_dma_memory.tx[index].w[3] & ETH_DESC_OWN)
			tx_mask |= 1UL << index;
		if (g_eth_dma_memory.rx[index].w[3] & ETH_DESC_OWN)
			rx_mask |= 1UL << index;
	}
	g_e35_metrics.tx_own_mask = tx_mask;
	g_e35_metrics.rx_own_mask = rx_mask;
}

void Error_Handler(void)
{
	g_e35_result = -100;
	__disable_irq();
	for (;
		 ;
		 )
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
		RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_D3PCLK1 | RCC_CLOCKTYPE_D1PCLK1;
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
