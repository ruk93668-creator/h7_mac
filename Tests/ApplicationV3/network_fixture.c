//仅替代以太网 MMIO 和链路，Network Service、Mailbox 和 lwIP 使用生产代码。
#include "eth_mac.h"
#include "platform_time.h"
#include <string.h>

static EthMacState_t s_state;
uint32_t g_network_stop_fail;
uint32_t g_network_link;
uint32_t g_network_init_calls;
uint32_t g_network_start_calls, g_network_stop_calls, g_network_samples;
uint32_t g_network_fault_samples;
EthResult_t g_network_fault_result;
uint32_t g_network_browser, g_network_browser_bytes;
static uint32_t s_peer_sequence, s_peer_ack;
uint32_t g_network_dhcp_server, g_network_discover, g_network_request, g_network_ack;
static uint8_t s_rx[4][600];
static uint16_t s_rx_length[4];
static unsigned s_rx_head, s_rx_count;

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
    ++g_network_stop_calls;
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
    ++g_network_start_calls;
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
    ++g_network_samples;
    if (g_network_fault_samples != 0U)
    {
        --g_network_fault_samples;
        if (g_network_fault_result != ETH_MDIO)
            s_state = ETH_MAC_FAULT;
        return g_network_fault_result;
    }
    return g_network_link ? ETH_OK : eth_mac_stop() == ETH_OK ? ETH_LINK_DOWN : ETH_TIMEOUT;
}

static void Fixture_Put16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

//只模拟线上的 DHCP Server，客户端解析、ACD 和定时器全部使用上游 lwIP。
static void Fixture_DhcpReply(const uint8_t *request, uint8_t type)
{
    uint8_t *frame, *ip, *udp, *dhcp, *option;
    unsigned slot, i;
    uint32_t sum = 0U;
    static const uint8_t server[4] = {192U, 168U, 50U, 1U};
    static const uint8_t offered[4] = {192U, 168U, 50U, 2U};
    if (s_rx_count == 4U)
        return;
    slot = (s_rx_head + s_rx_count) % 4U;
    frame = s_rx[slot];
    memset(frame, 0, 600U);
    memset(frame, 0xff, 6U);
    memcpy(frame + 6U, "\x02\x50\x00\x00\x00\x01", 6U);
    Fixture_Put16(frame + 12U, 0x0800U);
    ip = frame + 14U;
    udp = ip + 20U;
    dhcp = udp + 8U;
    dhcp[0] = 2U;
    dhcp[1] = 1U;
    dhcp[2] = 6U;
    memcpy(dhcp + 4U, request + 4U, 4U);
    memcpy(dhcp + 16U, offered, 4U);
    memcpy(dhcp + 20U, server, 4U);
    memcpy(dhcp + 28U, s_mac, 6U);
    memcpy(dhcp + 236U, "\x63\x82\x53\x63", 4U);
    option = dhcp + 240U;
    *option++ = 53U; *option++ = 1U; *option++ = type;
    *option++ = 54U; *option++ = 4U; memcpy(option, server, 4U); option += 4U;
    *option++ = 1U; *option++ = 4U;
    *option++ = 255U; *option++ = 255U; *option++ = 255U; *option++ = 0U;
    *option++ = 3U; *option++ = 4U; memcpy(option, server, 4U); option += 4U;
    *option++ = 51U; *option++ = 4U;
    *option++ = 0U; *option++ = 0U; *option++ = 0U; *option++ = 180U;
    *option++ = 58U; *option++ = 4U;
    *option++ = 0U; *option++ = 0U; *option++ = 0U; *option++ = 60U;
    *option++ = 59U; *option++ = 4U;
    *option++ = 0U; *option++ = 0U; *option++ = 0U; *option++ = 120U;
    *option++ = 255U;
    Fixture_Put16(udp, 67U);
    Fixture_Put16(udp + 2U, 68U);
    Fixture_Put16(udp + 4U, (uint16_t)(option - udp));
    ip[0] = 0x45U;
    ip[8] = 64U;
    ip[9] = 17U;
    Fixture_Put16(ip + 2U, (uint16_t)(option - ip));
    memcpy(ip + 12U, server, 4U);
    memset(ip + 16U, 255, 4U);
    for (i = 0U; i < 20U; i += 2U)
        sum += ((uint32_t)ip[i] << 8) | ip[i + 1U];
    while (sum >> 16)
        sum = (sum & 0xffffU) + (sum >> 16);
    Fixture_Put16(ip + 10U, (uint16_t)~sum);
    s_rx_length[slot] = (uint16_t)(option - frame);
    ++s_rx_count;
}

static uint16_t Fixture_Checksum(const uint8_t *data, unsigned length, uint32_t sum)
{
    unsigned i;
    for (i = 0U; i + 1U < length; i += 2U)
        sum += ((uint32_t)data[i] << 8) | data[i + 1U];
    if (length & 1U)
        sum += (uint32_t)data[length - 1U] << 8;
    while (sum >> 16)
        sum = (sum & 0xffffU) + (sum >> 16);
    return (uint16_t)~sum;
}

static uint32_t Fixture_Get32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
        ((uint32_t)data[2] << 8) | data[3];
}

static void Fixture_Put32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
}

static void Fixture_TcpPacket(uint32_t sequence, uint32_t ack,
    uint8_t flags, const char *payload)
{
    uint8_t *frame, *ip, *tcp;
    unsigned slot, i, length = payload != NULL ? strlen(payload) : 0U;
    uint32_t pseudo = 6U + 20U + length;
    if (s_rx_count == 4U || length > 500U)
        return;
    slot = (s_rx_head + s_rx_count) % 4U;
    frame = s_rx[slot];
    memset(frame, 0, 600U);
    memcpy(frame, s_mac, 6U);
    memcpy(frame + 6U, "\x02\x50\x00\x00\x00\x01", 6U);
    Fixture_Put16(frame + 12U, 0x0800U);
    ip = frame + 14U;
    tcp = ip + 20U;
    ip[0] = 0x45U;
    ip[8] = 64U;
    ip[9] = 6U;
    Fixture_Put16(ip + 2U, (uint16_t)(40U + length));
    memcpy(ip + 12U, "\xC0\xA8\x32\x01\xC0\xA8\x32\x02", 8U);
    Fixture_Put16(ip + 10U, Fixture_Checksum(ip, 20U, 0U));
    Fixture_Put16(tcp, 55000U);
    Fixture_Put16(tcp + 2U, 80U);
    Fixture_Put32(tcp + 4U, sequence);
    Fixture_Put32(tcp + 8U, ack);
    tcp[12] = 0x50U;
    tcp[13] = flags;
    Fixture_Put16(tcp + 14U, 12000U);
    if (length)
        memcpy(tcp + 20U, payload, length);
    for (i = 12U; i < 20U; i += 2U)
        pseudo += ((uint32_t)ip[i] << 8) | ip[i + 1U];
    Fixture_Put16(tcp + 16U, Fixture_Checksum(tcp, 20U + length, pseudo));
    s_rx_length[slot] = (uint16_t)(54U + length);
    ++s_rx_count;
}

void Test_NetworkBrowserOpen(void)
{
    s_peer_sequence = 101U;
    s_peer_ack = 0U;
    g_network_browser = 1U;
    Fixture_TcpPacket(100U, 0U, 2U, NULL);
}

void Test_NetworkBrowserReset(void)
{
    g_network_browser = 0U;
    Fixture_TcpPacket(s_peer_sequence, s_peer_ack, 0x14U, NULL);
}

EthResult_t eth_mac_tx_submit(const void *data, size_t length, uint32_t *ticket)
{
    const uint8_t *frame = data;
    size_t at, end;
    (void)ticket;
    if (!g_network_link)
        return ETH_LINK_DOWN;
    if (s_inflight == 4U)
        return ETH_BUSY;
    ++s_inflight;
    if (ticket != NULL)
        *ticket = s_inflight;
    if (g_network_browser && length >= 54U && frame[12] == 8U && frame[13] == 0U &&
        frame[23] == 6U && frame[36] == 0xd6U && frame[37] == 0xd8U)
    {
        const uint8_t *tcp = frame + 34U;
        unsigned ip_length = ((unsigned)frame[16] << 8) | frame[17];
        unsigned header = (tcp[12] >> 4) * 4U;
        unsigned payload = ip_length - 20U - header;
        uint8_t flags = tcp[13];
        uint32_t sequence = Fixture_Get32(tcp + 4U);
        if ((flags & 0x12U) == 0x12U)
        {
            static const char request[] = "GET /stream.mjpg HTTP/1.1\r\nHost: board\r\n\r\n";
            s_peer_ack = sequence + 1U;
            Fixture_TcpPacket(s_peer_sequence, s_peer_ack, 0x10U, NULL);
            Fixture_TcpPacket(s_peer_sequence, s_peer_ack, 0x18U, request);
            s_peer_sequence += sizeof(request) - 1U;
        }
        else if (payload || (flags & 1U))
        {
            g_network_browser_bytes += payload;
            s_peer_ack = sequence + payload + (flags & 1U);
            Fixture_TcpPacket(s_peer_sequence, s_peer_ack, 0x10U, NULL);
        }
    }
    if (g_network_dhcp_server && length >= 42U && frame[12] == 8U && frame[13] == 6U &&
        frame[20] == 0U && frame[21] == 1U &&
        memcmp(frame + 38U, "\xC0\xA8\x32\x01", 4U) == 0 && s_rx_count < 4U)
    {
        unsigned slot = (s_rx_head + s_rx_count) % 4U;
        uint8_t *reply = s_rx[slot];
        memcpy(reply, frame, 42U);
        memcpy(reply, s_mac, 6U);
        memcpy(reply + 6U, "\x02\x50\x00\x00\x00\x01", 6U);
        reply[21] = 2U;
        memcpy(reply + 22U, reply + 6U, 6U);
        memcpy(reply + 28U, "\xC0\xA8\x32\x01", 4U);
        memcpy(reply + 32U, s_mac, 6U);
        memcpy(reply + 38U, frame + 28U, 4U);
        s_rx_length[slot] = 42U;
        ++s_rx_count;
    }
    if (g_network_dhcp_server && length >= 282U && frame[12] == 8U && frame[13] == 0U &&
        frame[23] == 17U && frame[34] == 0U && frame[35] == 68U)
    {
        at = 14U + (frame[14] & 15U) * 4U + 8U;
        end = at + 240U;
        at = end;
        while (at < length && frame[at] != 255U)
        {
            uint8_t kind = frame[at++];
            uint8_t size;
            if (kind == 0U)
                continue;
            if (at == length || (size = frame[at++]) > length - at)
                break;
            if (kind == 53U && size == 1U)
            {
                if (frame[at] == 1U)
                {
                    ++g_network_discover;
                    Fixture_DhcpReply(frame + end - 240U, 2U);
                }
                else if (frame[at] == 3U)
                {
                    ++g_network_request;
                    ++g_network_ack;
                    Fixture_DhcpReply(frame + end - 240U, 5U);
                }
                break;
            }
            at += size;
        }
    }
    return ETH_OK;
}

EthResult_t eth_mac_tx_reclaim(uint32_t *ticket)
{
    (void)ticket;
    if (s_inflight)
    {
        --s_inflight;
        return ETH_OK;
    }
    return ETH_EMPTY;
}

EthResult_t eth_mac_rx_poll(void *data, size_t capacity, size_t *length)
{
    if (s_rx_count == 0U)
        return ETH_EMPTY;
    if (capacity < s_rx_length[s_rx_head])
        return ETH_ARGUMENT;
    *length = s_rx_length[s_rx_head];
    memcpy(data, s_rx[s_rx_head], *length);
    s_rx_head = (s_rx_head + 1U) % 4U;
    --s_rx_count;
    return ETH_OK;
}
