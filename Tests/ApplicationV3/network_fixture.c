//仅替代以太网 MMIO 和链路，Network Service、Mailbox 和 lwIP 使用生产代码。
#include "eth_mac.h"
#include "platform_time.h"
#include <string.h>

static EthMacState_t s_state;
uint32_t g_network_stop_fail;
uint32_t g_network_link;
uint32_t g_network_init_calls;
static uint8_t s_mac[6];
static uint32_t s_inflight;

int eth_memory_valid(void)
{
    return 1;
}

uint32_t sys_now(void)
{
    return Platform_TimeGetMs();
}

void eth_lwip_assert(const char *message, const char *file, unsigned line)
{
    extern volatile uint32_t g_failure;
    (void)message;
    (void)file;
    g_failure = line;
}

EthResult_t eth_mac_init(const uint8_t mac[6])
{
    ++g_network_init_calls;
    if (s_state != ETH_MAC_OFF)
        return ETH_STATE;
    memcpy(s_mac, mac, sizeof(s_mac));
    s_state = ETH_MAC_READY;
    return ETH_OK;
}

EthResult_t eth_mac_recover(const uint8_t mac[6])
{
    return eth_mac_init(mac);
}

EthResult_t eth_mac_stop(void)
{
    if (g_network_stop_fail)
        return ETH_TIMEOUT;
    if (s_state == ETH_MAC_OFF)
        return ETH_STATE;
    s_state = ETH_MAC_READY;
    s_inflight = 0U;
    return ETH_OK;
}

EthResult_t eth_mac_start(void)
{
    if (s_state != ETH_MAC_READY)
        return ETH_STATE;
    s_state = ETH_MAC_RUNNING;
    return ETH_OK;
}

EthResult_t eth_mac_get_statistics(EthStatistics_t *stats)
{
    memset(stats, 0, sizeof(*stats));
    stats->state = s_state;
    stats->phy_id = 0x7C131U;
    return ETH_OK;
}

EthResult_t eth_mac_get_link(EthLinkState_t *link)
{
    memset(link, 0, sizeof(*link));
    link->up = (uint8_t)g_network_link;
    link->autoneg_complete = 1U;
    link->full_duplex = 1U;
    link->speed_mbps = 100U;
    return ETH_OK;
}

EthResult_t eth_mac_get_address(uint8_t mac[6])
{
    memcpy(mac, s_mac, sizeof(s_mac));
    return ETH_OK;
}

EthResult_t eth_mac_service(void)
{
    return g_network_link ? ETH_OK : ETH_LINK_DOWN;
}

EthResult_t eth_mac_tx_submit(const void *data, size_t length, uint32_t *ticket)
{
    (void)data;
    (void)length;
    (void)ticket;
    return ETH_LINK_DOWN;
}

EthResult_t eth_mac_tx_reclaim(uint32_t *ticket)
{
    (void)ticket;
    return s_inflight ? ETH_BUSY : ETH_EMPTY;
}

EthResult_t eth_mac_rx_poll(void *data, size_t capacity, size_t *length)
{
    (void)data;
    (void)capacity;
    (void)length;
    return ETH_EMPTY;
}
