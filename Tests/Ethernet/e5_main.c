//独立 E5 固件；沿用 E4 测试时钟和 MPU，不启动生产应用
#include "stm32h7xx_hal.h"
#include "eth_memory.h"
#include "e5_bench.h"
#include "lwip/stats.h"

static EthNetwork_t s_network;
static const uint8_t s_mac[6] = {0x02U, 0x47U, 0x47U, 0U, 0U, 1U};

typedef struct { uint32_t used, peak, errors; } E5Pool_t;
typedef struct
{
	uint32_t heartbeat, ccr, memory_valid;
	ip4_addr_t current_ip;
	EthNetworkAddressStatus_t address;
	EthStatistics_t driver;
	EthTransportStats_t transport;
	E5ProtocolStats_t protocol[E5_CHANNELS];
	E5Pool_t pbuf, heap, tcp_pcb, tcp_listen, tcp_seg, udp_pcb, timers;
} E5Status_t;

volatile E5Status_t g_e5_status;
volatile int32_t g_e5_result;
volatile uint32_t g_e5_mode = ETH_ADDRESS_DHCP;
volatile uint8_t g_e5_peer[4] = {192U, 168U, 50U, 1U};
volatile uint16_t g_e5_client_port = 5004U;
volatile uint32_t g_e5_reconnect_ms = 2000U;
volatile uint32_t g_e5_send_interval_ms = 1000U;
const char *volatile g_e5_assert_message;

static void EthBench_Error(void)
{
	g_e5_result = -100;
	for (;;)
	{
	}
}

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
	(void)file;
	(void)line;
	g_e5_assert_message = message;
	EthBench_Error();
}

static void E5Pool(volatile E5Pool_t *out, const struct stats_mem *pool)
{
	out->used = pool->used;
	out->peak = pool->max;
	out->errors = pool->err;
}

static void E5Snapshot(void)
{
	EthTransportStats_t transport;
	EthStatistics_t driver;
	E5ProtocolStats_t protocol[E5_CHANNELS];
	unsigned index;

	g_e5_status.ccr = SCB->CCR;
	g_e5_status.memory_valid = (uint32_t)eth_memory_valid();
	g_e5_status.current_ip = *netif_ip4_addr(&s_network.interface);
	g_e5_status.address = s_network.address;
	if (eth_mac_get_statistics(&driver) == ETH_OK)
		g_e5_status.driver = driver;
	eth_transport_get_statistics(&transport);
	g_e5_status.transport = transport;
	e5_bench_statistics(protocol);
	for (index = 0; index < E5_CHANNELS; ++index)
		g_e5_status.protocol[index] = protocol[index];
	E5Pool(&g_e5_status.pbuf, lwip_stats.memp[MEMP_PBUF_POOL]);
	E5Pool(&g_e5_status.heap, &lwip_stats.mem);
	E5Pool(&g_e5_status.tcp_pcb, lwip_stats.memp[MEMP_TCP_PCB]);
	E5Pool(&g_e5_status.tcp_listen, lwip_stats.memp[MEMP_TCP_PCB_LISTEN]);
	E5Pool(&g_e5_status.tcp_seg, lwip_stats.memp[MEMP_TCP_SEG]);
	E5Pool(&g_e5_status.udp_pcb, lwip_stats.memp[MEMP_UDP_PCB]);
	E5Pool(&g_e5_status.timers, lwip_stats.memp[MEMP_SYS_TIMEOUT]);
}

int main(void)
{
	EthNetworkAddressConfig_t config = {.mode = ETH_ADDRESS_DHCP};
	ip4_addr_t peer;
	uint32_t last;

	if (HAL_Init() != HAL_OK)
		EthBench_Error();
	EthBench_Clock();
	EthBench_Mpu();
	SCB_EnableICache();
	SCB_EnableDCache();
	if (!eth_memory_valid() || g_e5_mode > ETH_ADDRESS_DHCP)
		EthBench_Error();
	config.mode = (EthNetworkAddressMode_t)g_e5_mode;
	config.timeout_ms = 30000U;
	IP4_ADDR(&config.ip, 192, 168, 50, 2);
	IP4_ADDR(&config.mask, 255, 255, 255, 0);
	IP4_ADDR(&config.gateway, 0, 0, 0, 0);
	IP4_ADDR(&peer, g_e5_peer[0], g_e5_peer[1], g_e5_peer[2], g_e5_peer[3]);
	g_e5_result = eth_network_init_address(&s_network, s_mac, &config);
	if (g_e5_result == ERR_OK)
		g_e5_result = e5_bench_init(&s_network, &peer, g_e5_client_port, g_e5_reconnect_ms);
	if (g_e5_result != ERR_OK)
	{
		for (;;)
		{
		}
	}
	last = sys_now();
	for (;;)
	{
		uint32_t now = sys_now();
		++g_e5_status.heartbeat;
		eth_network_poll(&s_network);
		eth_transport_poll();
		if (eth_transport_ready())
			e5_bench_poll(now, g_e5_send_interval_ms);
		if ((uint32_t)(now - last) >= 100U)
		{
			last = now;
			E5Snapshot();
		}
	}
}
