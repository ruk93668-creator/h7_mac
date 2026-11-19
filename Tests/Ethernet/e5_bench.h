#ifndef E5_BENCH_H
#define E5_BENCH_H

#include "eth_transport.h"

typedef struct
{
	uint32_t request_rx, reply_rx, reply_tx, request_tx;
	uint32_t invalid, duplicates, reordered, missing, blocked;
} E5ProtocolStats_t;

enum { E5_TCP_SERVER = 0, E5_TCP_CLIENT, E5_UDP, E5_CHANNELS };

err_t e5_bench_init(EthNetwork_t *net, const ip4_addr_t *peer, uint16_t client_port,
	uint32_t reconnect_ms);
void e5_bench_poll(uint32_t now, uint32_t interval);
void e5_bench_statistics(E5ProtocolStats_t stats[E5_CHANNELS]);

#endif
