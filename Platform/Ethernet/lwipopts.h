//独立 Ethernet 单循环协议栈配置
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
#ifdef ETH_LWIP_DHCP
#define LWIP_UDP 1
#define LWIP_DHCP 1
#ifdef ETH_LWIP_E5
#define MEMP_NUM_UDP_PCB 2
#else
#define MEMP_NUM_UDP_PCB 1
#endif
#else
#define LWIP_UDP 0
#define LWIP_DHCP 0
#endif
#ifdef ETH_LWIP_E5
#define LWIP_TCP 1
#define SO_REUSE 1
#define TCP_MSS 1460
#define TCP_WND (2 * TCP_MSS)
#define TCP_SND_BUF (2 * TCP_MSS)
#define TCP_SND_QUEUELEN 8
#define TCP_QUEUE_OOSEQ 0
#define MEMP_NUM_TCP_PCB 6
#define MEMP_NUM_TCP_PCB_LISTEN 1
#define MEMP_NUM_TCP_SEG 24
#define IP_SOF_BROADCAST 1
#else
#define LWIP_TCP 0
#endif
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
#ifdef ETH_LWIP_E5
#define MEM_SIZE (12 * 1024)
#define PBUF_POOL_SIZE 24
#else
#define MEM_SIZE (8 * 1024)
#define PBUF_POOL_SIZE 16
#endif
#define PBUF_POOL_BUFSIZE 512
#define LWIP_STATS 1
#define LWIP_STATS_DISPLAY 0

#endif
