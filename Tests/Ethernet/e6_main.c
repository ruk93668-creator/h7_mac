#include "stm32h7xx_hal.h"
#include "eth_memory.h"
#include "eth_http.h"
#include "eth_test_board.h"
#include "e6_jpeg_fixtures/e6_jpeg_fixtures.h"
#include "lwip/stats.h"

static EthNetwork_t s_network;
static const uint8_t s_mac[6] = {0x02U, 0x47U, 0x47U, 0U, 0U, 1U};

typedef struct { uint32_t used, peak, errors; } E6Pool_t;
typedef struct
{
	uint32_t heartbeat, ccr, memory_valid;
	ip4_addr_t current_ip;
	EthNetworkAddressStatus_t address;
	EthStatistics_t driver;
	EthTransportStats_t transport;
	EthHttpStats_t http;
	E6Pool_t pbuf, heap, tcp_pcb, tcp_listen, tcp_seg, udp_pcb, timers;
} E6Status_t;

volatile E6Status_t g_e6_status;
volatile int32_t g_e6_result;
volatile uint32_t g_e6_mode = ETH_ADDRESS_DHCP;
volatile uint16_t g_e6_period_ms = 200U;
const char *volatile g_e6_assert_message;

void eth_lwip_assert(const char *message, const char *file, unsigned line)
{
	(void)file;
	(void)line;
	g_e6_assert_message = message;
	g_e6_result = -100;
	for (;;) { }
}

static void E6Pool(volatile E6Pool_t *out, const struct stats_mem *pool)
{
	out->used = pool->used;
	out->peak = pool->max;
	out->errors = pool->err;
}

static void E6Snapshot(void)
{
	EthStatistics_t driver;
	EthTransportStats_t transport;
	EthHttpStats_t http;
	g_e6_status.ccr = SCB->CCR;
	g_e6_status.memory_valid = (uint32_t)eth_memory_valid();
	g_e6_status.current_ip = *netif_ip4_addr(&s_network.interface);
	g_e6_status.address = s_network.address;
	if (eth_mac_get_statistics(&driver) == ETH_OK)
		g_e6_status.driver = driver;
	eth_transport_get_statistics(&transport);
	g_e6_status.transport = transport;
	eth_http_get_statistics(&http);
	g_e6_status.http = http;
	E6Pool(&g_e6_status.pbuf, lwip_stats.memp[MEMP_PBUF_POOL]);
	E6Pool(&g_e6_status.heap, &lwip_stats.mem);
	E6Pool(&g_e6_status.tcp_pcb, lwip_stats.memp[MEMP_TCP_PCB]);
	E6Pool(&g_e6_status.tcp_listen, lwip_stats.memp[MEMP_TCP_PCB_LISTEN]);
	E6Pool(&g_e6_status.tcp_seg, lwip_stats.memp[MEMP_TCP_SEG]);
	E6Pool(&g_e6_status.udp_pcb, lwip_stats.memp[MEMP_UDP_PCB]);
	E6Pool(&g_e6_status.timers, lwip_stats.memp[MEMP_SYS_TIMEOUT]);
}

int main(void)
{
	EthNetworkAddressConfig_t config = {.mode = ETH_ADDRESS_DHCP};
	uint32_t last = 0U;
	if (!eth_test_board_init() || !eth_memory_valid() || g_e6_mode > ETH_ADDRESS_DHCP)
		eth_lwip_assert("E6 board setup", __FILE__, __LINE__);
	config.mode = (EthNetworkAddressMode_t)g_e6_mode;
	config.timeout_ms = 30000U;
	IP4_ADDR(&config.ip, 192, 168, 50, 2);
	IP4_ADDR(&config.mask, 255, 255, 255, 0);
	g_e6_result = eth_network_init_address(&s_network, s_mac, &config);
	if (g_e6_result == ERR_OK)
		g_e6_result = eth_http_init(&s_network, e6_jpeg_frames, E6_FRAME_COUNT, g_e6_period_ms);
	if (g_e6_result != ERR_OK)
		for (;;) { }
	for (;;)
	{
		uint32_t now = HAL_GetTick();
		++g_e6_status.heartbeat;
		eth_network_poll(&s_network);
		eth_transport_poll();
		eth_http_poll();
		if ((uint32_t)(now - last) >= 100U)
		{
			last = now;
			E6Snapshot();
		}
	}
}
