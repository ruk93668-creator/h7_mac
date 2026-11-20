//独立 E5 固件；沿用 E4 测试时钟和 MPU，不启动生产应用
#include "stm32h7xx_hal.h"
#include "eth_memory.h"
#include "e5_bench.h"
#include "eth_test_board.h"
#include "lwip/stats.h"
#include "lwip/sys.h"

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

	if (!eth_test_board_init() || !eth_memory_valid() || g_e5_mode > ETH_ADDRESS_DHCP)
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
