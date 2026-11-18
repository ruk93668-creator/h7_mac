"""Shared source lists for the E4.1 Keil and host validation builds."""

PORT = ['Platform/Ethernet/eth_netif.c', 'Platform/Ethernet/eth_network.c']
LWIP = ['ThirdParty/lwip/src/' + name for name in (
    'core/def.c', 'core/init.c', 'core/inet_chksum.c', 'core/ip.c',
    'core/mem.c', 'core/memp.c', 'core/netif.c', 'core/pbuf.c', 'core/raw.c',
    'core/stats.c', 'core/sys.c', 'core/timeouts.c',
    'core/ipv4/etharp.c', 'core/ipv4/icmp.c', 'core/ipv4/ip4.c',
    'core/ipv4/ip4_addr.c', 'netif/ethernet.c',
)]
