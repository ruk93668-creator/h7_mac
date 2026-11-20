//两个固定角色；lwIP PCB 不暴露给调用者
#include "eth_tcp.h"
#include "lwip/tcp.h"
#include "lwip/sys.h"
#include <string.h>

#define ETH_TCP_RETRY_MS 20U
#define ETH_TCP_CONNECT_MS 10000U
#define ETH_TCP_CLOSE_MS 5000U

typedef struct
{
	struct tcp_pcb *pcb;
	uint8_t rx[ETH_TCP_QUEUE_BYTES];
	uint8_t tx[ETH_TCP_QUEUE_BYTES];
	uint16_t rx_head, tx_head;
	uint32_t generation, activity, deadline, attempt;
	unsigned events;
	uint8_t peer_fin, output_pending;
	EthTcpStats_t stats;
} EthTcpConnection_t;

static struct tcp_pcb *s_listener;
static EthTcpConnection_t s_connections[ETH_TCP_ROLES];
static EthTcpEvent_t s_notify;
static void *s_notify_arg;
static uint32_t s_idle_ms;

static int EthTcp_Due(uint32_t now, uint32_t due)
{
	return (uint32_t)(now - due) < UINT32_C(0x80000000);
}

static EthTcpConnection_t *EthTcp_Connection(EthTcpHandle_t handle)
{
	if ((unsigned)handle.role >= ETH_TCP_ROLES ||
		s_connections[handle.role].generation != handle.generation)
		return NULL;
	return &s_connections[handle.role];
}

static void EthTcp_Clear(EthTcpConnection_t *connection, err_t error)
{
	connection->pcb = NULL;
	connection->stats.state = ETH_TCP_IDLE;
	connection->stats.tx_pending = 0U;
	connection->stats.rx_pending = 0U;
	connection->stats.unacked = 0U;
	connection->stats.last_error = error;
	connection->rx_head = 0U;
	connection->tx_head = 0U;
	connection->peer_fin = 0U;
	connection->output_pending = 0U;
	++connection->stats.closes;
	if (error != ERR_OK)
		++connection->stats.errors;
	connection->events |= ETH_TCP_EVENT_CLOSED;
}

static void EthTcp_Error(void *arg, err_t error)
{
	//错误回调进入时旧 PCB 已失效
	EthTcp_Clear(arg, error);
}

static err_t EthTcp_Receive(void *arg, struct tcp_pcb *pcb, struct pbuf *packet, err_t error)
{
	EthTcpConnection_t *connection = arg;
	uint16_t tail;
	uint16_t first;
	uint16_t length;
	(void)pcb;

	if (error != ERR_OK)
		return error;
	connection->activity = sys_now();
	if (!packet)
	{
		connection->peer_fin = 1U;
		connection->events |= ETH_TCP_EVENT_FIN;
		connection->deadline = connection->activity + ETH_TCP_CLOSE_MS;
		return ERR_OK;
	}
	length = packet->tot_len;
	if (length > ETH_TCP_QUEUE_BYTES - connection->stats.rx_pending)
	{
		++connection->stats.recv_full;
		return ERR_MEM; //未释放且未确认窗口，lwIP 将重新交付
	}
	tail = (uint16_t)((connection->rx_head + connection->stats.rx_pending) % ETH_TCP_QUEUE_BYTES);
	first = (uint16_t)(ETH_TCP_QUEUE_BYTES - tail);
	if (first > length)
		first = length;
	if (pbuf_copy_partial(packet, connection->rx + tail, first, 0U) != first ||
		pbuf_copy_partial(packet, connection->rx, (uint16_t)(length - first), first) != length - first)
		return ERR_MEM;
	connection->stats.rx_pending += length;
	if (connection->stats.rx_pending > connection->stats.rx_peak)
		connection->stats.rx_peak = connection->stats.rx_pending;
	connection->stats.received += length;
	connection->events |= ETH_TCP_EVENT_READ;
	pbuf_free(packet);
	return ERR_OK;
}

static err_t EthTcp_Sent(void *arg, struct tcp_pcb *pcb, u16_t length)
{
	EthTcpConnection_t *connection = arg;
	(void)pcb;
	if (length > connection->stats.unacked)
		length = connection->stats.unacked;
	connection->stats.unacked -= length;
	connection->stats.acked += length;
	connection->activity = sys_now();
	connection->attempt = connection->activity;
	connection->events |= ETH_TCP_EVENT_ACK;
	return ERR_OK;
}

static err_t EthTcp_RawPoll(void *arg, struct tcp_pcb *pcb)
{
	EthTcpConnection_t *connection = arg;
	(void)pcb;
	connection->attempt = sys_now();
	return ERR_OK;
}

static void EthTcp_Callbacks(EthTcpConnection_t *connection, int attach)
{
	struct tcp_pcb *pcb = connection->pcb;
	tcp_arg(pcb, attach ? connection : NULL);
	tcp_recv(pcb, attach ? EthTcp_Receive : NULL);
	tcp_sent(pcb, attach ? EthTcp_Sent : NULL);
	tcp_err(pcb, attach ? EthTcp_Error : NULL);
	tcp_poll(pcb, attach ? EthTcp_RawPoll : NULL, 2U);
}

static void EthTcp_Abort(EthTcpConnection_t *connection, err_t error)
{
	if (connection->pcb)
	{
		EthTcp_Callbacks(connection, 0);
		tcp_abort(connection->pcb);
		EthTcp_Clear(connection, error);
	}
}

static void EthTcp_Assign(EthTcpConnection_t *connection, struct tcp_pcb *pcb, EthTcpState_t state)
{
	connection->pcb = pcb;
	++connection->generation;
	if (!connection->generation)
		++connection->generation;
	connection->stats.state = state;
	connection->stats.last_error = ERR_OK;
	connection->activity = sys_now();
	connection->attempt = connection->activity;
	connection->deadline = connection->activity + ETH_TCP_CONNECT_MS;
	connection->events = 0U;
	EthTcp_Callbacks(connection, 1);
}

static void EthTcp_Opened(EthTcpConnection_t *connection)
{
	connection->stats.state = ETH_TCP_OPEN;
	connection->activity = sys_now();
	++connection->stats.opens;
	connection->events |= ETH_TCP_EVENT_OPEN;
}

static err_t EthTcp_Connected(void *arg, struct tcp_pcb *pcb, err_t error)
{
	(void)pcb;
	if (error == ERR_OK)
		EthTcp_Opened(arg);
	return error;
}

static err_t EthTcp_Accept(void *arg, struct tcp_pcb *pcb, err_t error)
{
	EthTcpConnection_t *connection = &s_connections[ETH_TCP_SERVER];
	(void)arg;
	if (error != ERR_OK || !pcb)
		return error;
	if (connection->pcb)
	{
		++connection->stats.rejected;
		tcp_abort(pcb);
		return ERR_ABRT;
	}
	EthTcp_Assign(connection, pcb, ETH_TCP_OPEN);
	EthTcp_Opened(connection);
	return ERR_OK;
}

err_t eth_tcp_configure(EthTcpEvent_t callback, void *arg, uint32_t idle_timeout_ms)
{
	if (s_listener || s_connections[ETH_TCP_SERVER].pcb || s_connections[ETH_TCP_CLIENT].pcb)
		return ERR_USE;
	if (idle_timeout_ms >= UINT32_C(0x80000000))
		return ERR_ARG;
	s_notify = callback;
	s_notify_arg = arg;
	s_idle_ms = idle_timeout_ms;
	return ERR_OK;
}

err_t eth_tcp_server_start(struct netif *netif, uint16_t port)
{
	struct tcp_pcb *pcb;
	err_t result;
	if (!netif || !port)
		return ERR_ARG;
	if (s_listener)
		return ERR_USE;
	pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
	if (!pcb)
		return ERR_MEM;
	//Link or address recovery must rebind the listener while old tuples remain in TIME_WAIT.
#if SO_REUSE
	ip_set_option(pcb, SOF_REUSEADDR);
#endif
	tcp_bind_netif(pcb, netif);
	result = tcp_bind(pcb, netif_ip4_addr(netif), port);
	if (result == ERR_OK)
		s_listener = tcp_listen_with_backlog_and_err(pcb, 1U, &result);
	if (!s_listener)
	{
		tcp_abort(pcb);
		return result;
	}
	tcp_accept(s_listener, EthTcp_Accept);
	return ERR_OK;
}

err_t eth_tcp_client_connect(struct netif *netif, const ip4_addr_t *peer, uint16_t port)
{
	struct tcp_pcb *pcb;
	err_t result;
	EthTcpConnection_t *connection = &s_connections[ETH_TCP_CLIENT];
	if (!netif || !peer || !port || ip4_addr_isany(peer))
		return ERR_ARG;
	if (connection->pcb)
		return ERR_ISCONN;
	pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
	if (!pcb)
		return ERR_MEM;
	tcp_bind_netif(pcb, netif);
	EthTcp_Assign(connection, pcb, ETH_TCP_CONNECTING);
	result = tcp_connect(pcb, peer, port, EthTcp_Connected);
	if (result != ERR_OK)
		EthTcp_Abort(connection, result);
	return result;
}

void eth_tcp_stop(void)
{
	unsigned role;
	for (role = 0; role < ETH_TCP_ROLES; ++role)
		EthTcp_Abort(&s_connections[role], ERR_RTE);
	if (s_listener)
	{
		tcp_close(s_listener);
		s_listener = NULL;
	}
}

static void EthTcp_PollConnection(EthTcpConnection_t *connection, uint32_t now)
{
	EthTcpStats_t *stats = &connection->stats;
	struct tcp_pcb *pcb = connection->pcb;
	err_t result;
	uint16_t count;

	if (!pcb)
		return;
	if (stats->state == ETH_TCP_CONNECTING)
	{
		if (EthTcp_Due(now, connection->deadline))
		{
			++stats->timeouts;
			EthTcp_Abort(connection, ERR_TIMEOUT);
		}
		return;
	}
	if ((stats->state == ETH_TCP_DRAINING || connection->peer_fin) ?
		EthTcp_Due(now, connection->deadline) :
		(s_idle_ms && (uint32_t)(now - connection->activity) >= s_idle_ms))
	{
		++stats->timeouts;
		EthTcp_Abort(connection, ERR_TIMEOUT);
		return;
	}
	if (!EthTcp_Due(now, connection->attempt))
		return;
	connection->attempt = now + ETH_TCP_RETRY_MS;
	if ((stats->state == ETH_TCP_DRAINING || connection->peer_fin) &&
		!stats->tx_pending && !stats->unacked && !stats->rx_pending)
	{
		EthTcp_Callbacks(connection, 0);
		result = tcp_close(pcb);
		if (result == ERR_OK)
			EthTcp_Clear(connection, ERR_OK);
		else
		{
			EthTcp_Callbacks(connection, 1);
			if (result == ERR_MEM)
				++stats->close_mem;
			else
				EthTcp_Abort(connection, result);
		}
		return;
	}
	count = stats->tx_pending;
	if (count > tcp_sndbuf(pcb))
		count = tcp_sndbuf(pcb);
	if (count > ETH_TCP_QUEUE_BYTES - connection->tx_head)
		count = (uint16_t)(ETH_TCP_QUEUE_BYTES - connection->tx_head);
	if (count > TCP_MSS)
		count = TCP_MSS;
	if (count && tcp_sndqueuelen(pcb) < TCP_SND_QUEUELEN)
	{
		result = tcp_write(pcb, connection->tx + connection->tx_head, count, TCP_WRITE_FLAG_COPY);
		if (result == ERR_OK)
		{
			connection->tx_head = (uint16_t)((connection->tx_head + count) % ETH_TCP_QUEUE_BYTES);
			stats->tx_pending -= count;
			stats->queued_lwip += count;
			stats->unacked += count;
			if (stats->unacked > stats->unacked_peak)
				stats->unacked_peak = stats->unacked;
			connection->output_pending = 1U;
		}
		else if (result == ERR_MEM)
			++stats->write_mem;
		else
		{
			EthTcp_Abort(connection, result);
			return;
		}
	}
	if (connection->output_pending)
	{
		result = tcp_output(pcb);
		if (result == ERR_OK)
			connection->output_pending = 0U;
		else
			++stats->output_errors;
	}
}

void eth_tcp_poll(void)
{
	unsigned role;
	for (role = 0; role < ETH_TCP_ROLES; ++role)
	{
		EthTcpConnection_t *connection = &s_connections[role];
		unsigned events;
		EthTcp_PollConnection(connection, sys_now());
		events = connection->events;
		connection->events = 0;
		if (events && s_notify)
			s_notify(eth_tcp_handle((EthTcpRole_t)role), events, s_notify_arg);
	}
}

EthTcpHandle_t eth_tcp_handle(EthTcpRole_t role)
{
	EthTcpHandle_t handle;
	handle.role = role;
	handle.generation = (unsigned)role < ETH_TCP_ROLES ? s_connections[role].generation : 0U;
	return handle;
}

EthTcpState_t eth_tcp_get_state(EthTcpHandle_t handle)
{
	EthTcpConnection_t *connection = EthTcp_Connection(handle);
	return connection ? connection->stats.state : ETH_TCP_IDLE;
}

err_t eth_tcp_send(EthTcpHandle_t handle, const void *data, uint16_t length)
{
	EthTcpConnection_t *connection = EthTcp_Connection(handle);
	uint16_t tail, first;
	if (!data || !length || length > ETH_TCP_QUEUE_BYTES)
		return ERR_ARG;
	if (!connection || !connection->pcb || connection->stats.state != ETH_TCP_OPEN)
		return ERR_CONN;
	if (length > ETH_TCP_QUEUE_BYTES - connection->stats.tx_pending)
	{
		++connection->stats.send_full;
		return ERR_MEM;
	}
	tail = (uint16_t)((connection->tx_head + connection->stats.tx_pending) % ETH_TCP_QUEUE_BYTES);
	first = (uint16_t)(ETH_TCP_QUEUE_BYTES - tail);
	if (first > length)
		first = length;
	memcpy(connection->tx + tail, data, first);
	memcpy(connection->tx, (const uint8_t *)data + first, length - first);
	connection->stats.tx_pending += length;
	if (connection->stats.tx_pending > connection->stats.tx_peak)
		connection->stats.tx_peak = connection->stats.tx_pending;
	connection->stats.submitted += length;
	return ERR_OK;
}

uint16_t eth_tcp_read(EthTcpHandle_t handle, void *data, uint16_t capacity)
{
	EthTcpConnection_t *connection = EthTcp_Connection(handle);
	uint16_t first;
	if (!connection || !connection->pcb || !data || !capacity)
		return 0U;
	if (capacity > connection->stats.rx_pending)
		capacity = connection->stats.rx_pending;
	first = (uint16_t)(ETH_TCP_QUEUE_BYTES - connection->rx_head);
	if (first > capacity)
		first = capacity;
	memcpy(data, connection->rx + connection->rx_head, first);
	memcpy((uint8_t *)data + first, connection->rx, capacity - first);
	connection->rx_head = (uint16_t)((connection->rx_head + capacity) % ETH_TCP_QUEUE_BYTES);
	connection->stats.rx_pending -= capacity;
	connection->stats.read += capacity;
	if (capacity)
		tcp_recved(connection->pcb, capacity);
	return capacity;
}

err_t eth_tcp_close(EthTcpHandle_t handle)
{
	EthTcpConnection_t *connection = EthTcp_Connection(handle);
	if (!connection || !connection->pcb)
		return ERR_CONN;
	if (connection->stats.state == ETH_TCP_DRAINING)
		return ERR_ALREADY;
	if (connection->stats.state != ETH_TCP_OPEN)
		return ERR_INPROGRESS;
	connection->stats.state = ETH_TCP_DRAINING;
	connection->deadline = sys_now() + ETH_TCP_CLOSE_MS;
	return ERR_OK;
}

void eth_tcp_abort(EthTcpHandle_t handle)
{
	EthTcpConnection_t *connection = EthTcp_Connection(handle);
	if (connection)
		EthTcp_Abort(connection, ERR_ABRT);
}

void eth_tcp_get_statistics(EthTcpRole_t role, EthTcpStats_t *stats)
{
	if (stats && (unsigned)role < ETH_TCP_ROLES)
		*stats = s_connections[role].stats;
}

int eth_tcp_listening(void)
{
	return s_listener != NULL;
}
