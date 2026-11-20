"""Shared source lists for the isolated E4 Keil and host builds."""

PORT = ['Platform/Ethernet/eth_netif.c', 'Platform/Ethernet/eth_network.c']
LWIP = ['ThirdParty/lwip/src/' + name for name in (
    'core/def.c', 'core/init.c', 'core/inet_chksum.c', 'core/ip.c',
    'core/mem.c', 'core/memp.c', 'core/netif.c', 'core/pbuf.c', 'core/raw.c',
    'core/stats.c', 'core/sys.c', 'core/timeouts.c',
    'core/ipv4/etharp.c', 'core/ipv4/icmp.c', 'core/ipv4/ip4.c',
    'core/ipv4/ip4_addr.c', 'netif/ethernet.c',
)]
LWIP_DHCP = ['ThirdParty/lwip/src/' + name for name in
             ('core/udp.c', 'core/ipv4/dhcp.c', 'core/ipv4/acd.c')]
LWIP_TCP = ['ThirdParty/lwip/src/core/' + name + '.c' for name in ('tcp', 'tcp_in', 'tcp_out')]
TRANSPORT = ['Platform/Ethernet/' + name + '.c' for name in ('eth_udp', 'eth_tcp', 'eth_transport')]
BENCH = ['Tests/Ethernet/e5_protocol.c', 'Tests/Ethernet/e5_bench.c']
BOARD = ['Tests/Ethernet/eth_test_board.c']
HTTP = ['Platform/Ethernet/eth_http.c', 'Platform/Ethernet/eth_mjpeg.c',
        'Tests/Ethernet/e6_jpeg_fixtures/e6_jpeg_fixtures.c']
