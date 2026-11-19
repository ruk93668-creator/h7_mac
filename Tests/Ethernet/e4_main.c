//独立 E4 测试入口，不启动 RTOS 或 Application
#include "stm32h7xx_hal.h"
#include "eth_memory.h"
#include "eth_network.h"
#include "lwip/stats.h"

static EthNetwork_t s_network;
static const uint8_t s_mac[6] = {0x02U, 0x47U, 0x47U, 0U, 0U, 1U};

typedef struct
{
	uint32_t heartbeat;
	uint32_t ccr;
	uint32_t memory_valid;
	uint32_t admin_up;
	uint32_t link_up;
	uint32_t link_ups;
	uint32_t link_downs;
	uint32_t pool_used;
	uint32_t pool_peak;
	uint32_t pool_errors;
	EthResult_t driver_result;
	EthLinkState_t phy;
	EthStatistics_t driver;
	EthNetifStatistics_t port;
#if LWIP_DHCP
	EthNetworkAddressStatus_t address;
	uint32_t udp_used;
	uint32_t udp_peak;
	uint32_t udp_errors;
	uint32_t timeout_used;
	uint32_t timeout_errors;
	uint32_t heap_used;
	uint32_t heap_peak;
	uint32_t heap_errors;
#endif
} EthBenchStatus_t;

volatile EthBenchStatus_t g_e4_status;
volatile int32_t g_e4_result;
const char * volatile g_e4_assert_message;
const char * volatile g_e4_assert_file;
volatile unsigned g_e4_assert_line;
#if LWIP_DHCP
//仅在初始化前读取一次；默认 DHCP Only，fallback 必须填写具体地址和掩码
volatile uint32_t g_e42_mode = ETH_ADDRESS_DHCP;
volatile uint32_t g_e42_timeout_ms = 30000U;
volatile uint8_t g_e42_fallback_ip[4];
volatile uint8_t g_e42_fallback_mask[4];
volatile uint8_t g_e42_fallback_gateway[4];
#endif

static void EthBench_Clock(void);
static void EthBench_Error(void);

static void EthBench_Mpu(void)
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
	HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

u32_t sys_now(void)
{
	return HAL_GetTick();
}

void SysTick_Handler(void)
{
	HAL_IncTick();
}

void eth_lwip_assert(const char *message, const char *file, unsigned line)
{
	g_e4_assert_message = message;
	g_e4_assert_file = file;
	g_e4_assert_line = line;
	g_e4_result = -101;
	for (;;)
	{
	}
}

static void EthBench_Snapshot(void)
{
	EthStatistics_t driver;

	g_e4_status.ccr = SCB->CCR;
	g_e4_status.memory_valid = (uint32_t)eth_memory_valid();
	g_e4_status.admin_up = (uint32_t)(netif_is_up(&s_network.interface) != 0);
	g_e4_status.link_up = (uint32_t)(netif_is_link_up(&s_network.interface) != 0);
	g_e4_status.link_ups = s_network.link_ups;
	g_e4_status.link_downs = s_network.link_downs;
	g_e4_status.driver_result = s_network.last_driver_result;
	g_e4_status.phy = s_network.phy;
	g_e4_status.port = s_network.port.statistics;
	g_e4_status.pool_used = lwip_stats.memp[MEMP_PBUF_POOL]->used;
	g_e4_status.pool_peak = lwip_stats.memp[MEMP_PBUF_POOL]->max;
	g_e4_status.pool_errors = lwip_stats.memp[MEMP_PBUF_POOL]->err;
#if LWIP_DHCP
	g_e4_status.address = s_network.address;
	g_e4_status.udp_used = lwip_stats.memp[MEMP_UDP_PCB]->used;
	g_e4_status.udp_peak = lwip_stats.memp[MEMP_UDP_PCB]->max;
	g_e4_status.udp_errors = lwip_stats.memp[MEMP_UDP_PCB]->err;
	g_e4_status.timeout_used = lwip_stats.memp[MEMP_SYS_TIMEOUT]->used;
	g_e4_status.timeout_errors = lwip_stats.memp[MEMP_SYS_TIMEOUT]->err;
	g_e4_status.heap_used = lwip_stats.mem.used;
	g_e4_status.heap_peak = lwip_stats.mem.max;
	g_e4_status.heap_errors = lwip_stats.mem.err;
#endif
	if (eth_mac_get_statistics(&driver) == ETH_OK)
		g_e4_status.driver = driver;
}

int main(void)
{
	ip4_addr_t ip;
	ip4_addr_t mask;
	ip4_addr_t gateway;
	uint32_t last;
#if LWIP_DHCP
	EthNetworkAddressConfig_t config = {.mode = ETH_ADDRESS_DHCP};
#endif

	if (HAL_Init() != HAL_OK)
		EthBench_Error();
	EthBench_Clock();
	EthBench_Mpu();
	SCB_EnableICache();
	SCB_EnableDCache();
	if (!eth_memory_valid())
		EthBench_Error();

	IP4_ADDR(&ip, 192, 168, 50, 2);
	IP4_ADDR(&mask, 255, 255, 255, 0);
	IP4_ADDR(&gateway, 0, 0, 0, 0);
#if LWIP_DHCP
	if (g_e42_mode > ETH_ADDRESS_DHCP_FALLBACK)
	{
		g_e4_result = ERR_ARG;
		for (;;)
		{
		}
	}
	config.mode = (EthNetworkAddressMode_t)g_e42_mode;
	config.timeout_ms = g_e42_timeout_ms;
	if (g_e42_mode == ETH_ADDRESS_STATIC)
	{
		config.ip = ip;
		config.mask = mask;
		config.gateway = gateway;
	}
	else if (g_e42_mode == ETH_ADDRESS_DHCP_FALLBACK)
	{
		IP4_ADDR(&config.ip, g_e42_fallback_ip[0], g_e42_fallback_ip[1],
			g_e42_fallback_ip[2], g_e42_fallback_ip[3]);
		IP4_ADDR(&config.mask, g_e42_fallback_mask[0], g_e42_fallback_mask[1],
			g_e42_fallback_mask[2], g_e42_fallback_mask[3]);
		IP4_ADDR(&config.gateway, g_e42_fallback_gateway[0], g_e42_fallback_gateway[1],
			g_e42_fallback_gateway[2], g_e42_fallback_gateway[3]);
	}
	g_e4_result = eth_network_init_address(&s_network, s_mac, &config);
#else
	g_e4_result = eth_network_init(&s_network, s_mac, &ip, &mask, &gateway);
#endif
	if (g_e4_result != ERR_OK)
	{
		g_e4_status.driver_result = s_network.last_driver_result;
		for (;;)
		{
		}
	}
	last = HAL_GetTick();
	EthBench_Snapshot();
	for (;;)
	{
		uint32_t now = HAL_GetTick();

		++g_e4_status.heartbeat;
		eth_network_poll(&s_network);
		if ((uint32_t)(now - last) >= 100U)
		{
			last = now;
			EthBench_Snapshot();
		}
	}
}

static void EthBench_Error(void)
{
	g_e4_result = -100;
	for (;;)
	{
	}
}

static void EthBench_Clock(void)
{
	RCC_OscInitTypeDef osc = {0};
	RCC_ClkInitTypeDef clock = {0};

	if (HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY) != HAL_OK)
		EthBench_Error();
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
		EthBench_Error();

	clock.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 |
		RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_D3PCLK1 | RCC_CLOCKTYPE_D1PCLK1;
	clock.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
	clock.SYSCLKDivider = RCC_SYSCLK_DIV1;
	clock.AHBCLKDivider = RCC_HCLK_DIV2;
	clock.APB3CLKDivider = RCC_APB3_DIV2;
	clock.APB1CLKDivider = RCC_APB1_DIV2;
	clock.APB2CLKDivider = RCC_APB2_DIV2;
	clock.APB4CLKDivider = RCC_APB4_DIV2;
	if (HAL_RCC_ClockConfig(&clock, FLASH_LATENCY_4) != HAL_OK)
		EthBench_Error();
}
