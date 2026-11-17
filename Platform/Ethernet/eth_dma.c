//描述符环和内部缓冲区管理
#include "eth_dma.h"
#include <string.h>
typedef char descriptor_size_check[(sizeof(eth_descriptor) == 32U) ? 1 : -1];
typedef char buffer_alignment_check[(ETH_BUFFER_SIZE % 32U == 0U) ? 1 : -1];
typedef char rx_descriptor_offset_check[(offsetof(eth_dma_memory, rx) == 128U) ? 1 : -1];
typedef char tx_buffer_offset_check[(offsetof(eth_dma_memory, tx_buffer) == 256U) ? 1 : -1];
typedef char rx_buffer_offset_check[(offsetof(eth_dma_memory, rx_buffer) == 6400U) ? 1 : -1];
//转换硬件使用的 32 位地址
static uint32_t address(const void *p)
{
	return (uint32_t)(uintptr_t)p;
}
//推进环索引并处理回绕
static unsigned next(unsigned i)
{
	return (i + 1U) % ETH_RING_SIZE;
}
//检查调用者内存是否与 DMA 对象重叠
static int dma_alias(const eth_ring *r, const void *p, size_t n)
{
	uintptr_t a = (uintptr_t)p, b = (uintptr_t)r->memory;
	return a <= b ? b - a < n : a - b < sizeof(*r->memory);
}
//保证数据、OWN 和尾指针的发布顺序
static void barrier(eth_ring *r)
{
	r->ops.barrier(r->ops.context);
}
//重备接收地址并把描述符发布给 DMA
static void rx_post(eth_ring *r, unsigned i)
{
	eth_descriptor *d = &r->memory->rx[i];
	d->w[0] = address(r->memory->rx_buffer[i]);
	d->w[1] = 0U;
	d->w[2] = 0U;
	barrier(r);
	d->w[3] = ETH_RX_POST;
	barrier(r);
}
//仅在硬件停止后重建描述符和软件索引
eth_result eth_ring_reset(eth_ring *r, eth_dma_memory *m, const eth_dma_ops *ops)
{
	unsigned i;
	if (!r || !m || !ops || !ops->barrier || !ops->tx_kick || !ops->rx_kick ||
		((uintptr_t)m & 31U) != 0U)
		return ETH_ARGUMENT;
	//DMA 停止并确认复位后才能重建，丢弃的发送由上层统计
	memset(r, 0, sizeof(*r));
	r->memory = m;
	r->ops = *ops;
	memset(m, 0, sizeof(*m));
	r->rx_guard = ETH_RING_CAPACITY;
	for (i = 0U; i < ETH_RING_CAPACITY; ++i)
		rx_post(r, i);
	return ETH_OK;
}
//复制发送数据并发布描述符所有权
eth_result eth_ring_tx(eth_ring *r, const void *data, size_t length, uint32_t *ticket)
{
	eth_descriptor *d;
	unsigned i;
	if (!r || !r->memory || !data || !ticket || length < 14U || length > ETH_FRAME_MAX)
		return ETH_ARGUMENT;
	if (dma_alias(r, data, length) || dma_alias(r, ticket, sizeof(*ticket)))
		return ETH_ARGUMENT;
	if (r->tx_count == ETH_RING_CAPACITY)
	{
		++r->statistics.tx_full;
		return ETH_BUSY;
	}
	i = r->tx_head;
	d = &r->memory->tx[i];
	if (r->pending[i] || (d->w[3] & ETH_DESC_OWN) != 0U)
	{
		++r->statistics.ownership_errors;
		return ETH_STATE;
	}
	barrier(r);
	memcpy(r->memory->tx_buffer[i], data, length);
	d->w[0] = address(r->memory->tx_buffer[i]);
	d->w[1] = 0U;
	d->w[2] = UINT32_C(0x80000000) | (uint32_t)length; //使能完成中断并填写缓冲区长度
	r->pending[i] = 1U;
	r->ticket[i] = ++r->sequence;
	*ticket = r->ticket[i];
	++r->tx_count;
	r->tx_head = next(i);
	++r->statistics.tx_submitted;
	barrier(r);
	d->w[3] = ETH_DESC_OWN | ETH_DESC_FD | ETH_DESC_LD | (uint32_t)length;
	barrier(r);
	//尾指针指向下一个描述符，OWN 仍限制 DMA 访问
	r->ops.tx_kick(r->ops.context, address(&r->memory->tx[r->tx_head]));
	return ETH_OK;
}
//按提交顺序回收已完成的发送槽
eth_result eth_ring_reclaim(eth_ring *r, uint32_t *ticket)
{
	uint32_t status;
	unsigned i;
	eth_result result = ETH_OK;
	if (!r || !r->memory || !ticket)
		return ETH_ARGUMENT;
	if (dma_alias(r, ticket, sizeof(*ticket)))
		return ETH_ARGUMENT;
	if (r->tx_count == 0U)
		return ETH_EMPTY;
	i = r->tx_tail;
	status = r->memory->tx[i].w[3];
	if (!r->pending[i])
	{
		++r->statistics.ownership_errors;
		return ETH_STATE;
	}
	if ((status & ETH_DESC_OWN) != 0U)
		return ETH_EMPTY;
	barrier(r);
	if ((status & (ETH_DESC_ES | UINT32_C(0x40000000))) != 0U ||
		(status & (ETH_DESC_FD | ETH_DESC_LD)) != (ETH_DESC_FD | ETH_DESC_LD))
	{
		++r->statistics.tx_errors;
		result = ETH_FRAME_ERROR;
	}
	else
		++r->statistics.tx_completed;
	*ticket = r->ticket[i];
	r->pending[i] = 0U;
	--r->tx_count;
	r->tx_tail = next(i);
	return result;
}
//复制有效接收帧并轮转接收 guard
eth_result eth_ring_rx(eth_ring *r, void *data, size_t capacity, size_t *length)
{
	unsigned i;
	uint32_t s;
	size_t n;
	eth_result result;
	if (!r || !r->memory || !data || !length)
		return ETH_ARGUMENT;
	if (dma_alias(r, data, capacity) || dma_alias(r, length, sizeof(*length)))
		return ETH_ARGUMENT;
	*length = 0U;
	i = r->rx_head;
	s = r->memory->rx[i].w[3];
	if ((s & ETH_DESC_OWN) != 0U)
		return ETH_EMPTY;
	barrier(r);
	n = (size_t)(s & UINT32_C(0x7FFF));
	if ((s & ETH_DESC_FD) != 0U)
		r->dropping = 0U;
	if (r->dropping || (s & (ETH_DESC_FD | ETH_DESC_LD)) != (ETH_DESC_FD | ETH_DESC_LD) ||
		(s & (ETH_DESC_ES | UINT32_C(0x40000000))) != 0U || n < 14U || n > ETH_FRAME_MAX)
	{
		++r->statistics.rx_errors;
		result = ETH_FRAME_ERROR;
		r->dropping = (uint8_t)((s & ETH_DESC_LD) == 0U);
	}
	else
	{
		*length = n;
		if (capacity < n)
		{
			++r->statistics.rx_small;
			return ETH_CAPACITY;
		}
		memcpy(data, r->memory->rx_buffer[i], n);
		++r->statistics.rx_delivered;
		result = ETH_OK;
	}
	rx_post(r, r->rx_guard);
	r->rx_guard = i;
	r->rx_head = next(i);
	//尾指针指向新 guard，归还旧 guard 的接收容量
	r->ops.rx_kick(r->ops.context, address(&r->memory->rx[r->rx_guard]));
	return result;
}
