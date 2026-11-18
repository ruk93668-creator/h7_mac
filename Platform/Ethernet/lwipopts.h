//E4.1 单循环协议栈配置
#ifndef ETH_LWIPOPTS_H
#define ETH_LWIPOPTS_H

#define NO_SYS 1
#define SYS_LIGHTWEIGHT_PROT 0
#define LWIP_TIMERS 1
#define MEMP_NUM_SYS_TIMEOUT (LWIP_NUM_SYS_TIMEOUT_INTERNAL + 2)
#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define LWIP_ETHERNET 1
#define LWIP_ARP 1
#define LWIP_ICMP 1
#define LWIP_RAW 1
#define LWIP_UDP 0
#define LWIP_TCP 0
#define LWIP_DHCP 0
#define LWIP_AUTOIP 0
#define LWIP_DNS 0
#define LWIP_IGMP 0
#define LWIP_UDPLITE 0
#define LWIP_NETCONN 0
#define LWIP_SOCKET 0
#define IP_REASSEMBLY 0
#define IP_FRAG 0
#define ETHARP_SUPPORT_VLAN 0
#define ETH_PAD_SIZE 0
#if defined(_WIN64)
#define MEM_ALIGNMENT 8
#else
#define MEM_ALIGNMENT 4
#endif
#define MEM_SIZE (8 * 1024)
#define PBUF_POOL_SIZE 16
#define PBUF_POOL_BUFSIZE 512
#define LWIP_STATS 1
#define LWIP_STATS_DISPLAY 0

#endif
