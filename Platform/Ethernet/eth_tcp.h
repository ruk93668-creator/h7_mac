#ifndef ETH_TCP_PORT_H
#define ETH_TCP_PORT_H

#include "lwip/netif.h"

#define ETH_TCP_QUEUE_BYTES 2920U

typedef enum { ETH_TCP_SERVER = 0, ETH_TCP_CLIENT, ETH_TCP_ROLES } EthTcpRole_t;
typedef enum { ETH_TCP_IDLE = 0, ETH_TCP_CONNECTING, ETH_TCP_OPEN, ETH_TCP_DRAINING } EthTcpState_t;
typedef struct { EthTcpRole_t role; uint32_t generation; } EthTcpHandle_t;
enum { ETH_TCP_EVENT_OPEN = 1, ETH_TCP_EVENT_READ = 2, ETH_TCP_EVENT_ACK = 4,
	ETH_TCP_EVENT_FIN = 8, ETH_TCP_EVENT_CLOSED = 16 };
typedef void (*EthTcpEvent_t)(EthTcpHandle_t handle, unsigned events, void *arg);
typedef struct
{
	uint32_t opens, closes, errors, timeouts, rejected;
	uint32_t submitted, queued_lwip, acked, received, read;
	uint32_t send_full, write_mem, recv_full, close_mem, output_errors;
	uint16_t tx_pending, rx_pending, unacked;
	uint16_t tx_peak, rx_peak, unacked_peak;
	err_t last_error;
	EthTcpState_t state;
} EthTcpStats_t;

err_t eth_tcp_configure(EthTcpEvent_t callback, void *arg, uint32_t idle_timeout_ms);
err_t eth_tcp_server_start(struct netif *netif, uint16_t port);
err_t eth_tcp_client_connect(struct netif *netif, const ip4_addr_t *peer, uint16_t port);
void eth_tcp_stop(void);
void eth_tcp_poll(void);
EthTcpHandle_t eth_tcp_handle(EthTcpRole_t role);
EthTcpState_t eth_tcp_get_state(EthTcpHandle_t handle);
err_t eth_tcp_send(EthTcpHandle_t handle, const void *data, uint16_t length);
uint16_t eth_tcp_read(EthTcpHandle_t handle, void *data, uint16_t capacity);
err_t eth_tcp_close(EthTcpHandle_t handle);
void eth_tcp_abort(EthTcpHandle_t handle);
void eth_tcp_get_statistics(EthTcpRole_t role, EthTcpStats_t *stats);
int eth_tcp_listening(void);

#endif
