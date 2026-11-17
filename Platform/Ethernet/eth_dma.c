//描述符环和内部缓冲区管理
#include "eth_dma.h"
#include <string.h>
typedef char descriptor_size_check[(sizeof(EthDmaDescriptor_t) == 32U) ? 1 : -1];
typedef char buffer_alignment_check[(ETH_BUFFER_SIZE % 32U == 0U) ? 1 : -1];
typedef char rx_descriptor_offset_check[(offsetof(EthDmaMemory_t, rx) == 128U) ? 1 : -1];
typedef char tx_buffer_offset_check[(offsetof(EthDmaMemory_t, tx_buffer) == 256U) ? 1 : -1];
typedef char rx_buffer_offset_check[(offsetof(EthDmaMemory_t, rx_buffer) == 6400U) ? 1 : -1];

//内部功能函数声明
static uint32_t EthDma_Address(const void *pointer);

static unsigned EthDma_NextIndex(unsigned index);

static int EthDma_MemoryOverlaps(
	const EthDmaRing_t *ring,
	const void *pointer,
	size_t length
);

static void EthDma_Barrier(EthDmaRing_t *ring);

static void EthDma_PostRx(
	EthDmaRing_t *ring,
	unsigned index
);

//对外接口实现

//仅在硬件停止后重建描述符和软件索引
EthResult_t eth_ring_reset(
	EthDmaRing_t *ring,
	EthDmaMemory_t *memory,
	const EthDmaOps_t *ops
)
{
	unsigned index;

	if (ring == NULL || memory == NULL || ops == NULL || ops->barrier == NULL ||
		ops->tx_kick == NULL || ops->rx_kick == NULL || ((uintptr_t)memory & 31U) != 0U)
		return ETH_ARGUMENT;
	//DMA 停止并确认复位后才能重建，丢弃的发送由上层统计
	memset(ring, 0, sizeof(*ring));
	ring->memory = memory;
	ring->ops = *ops;
	memset(memory, 0, sizeof(*memory));
	ring->rx_guard = ETH_RING_CAPACITY;
	for (index = 0U;
		 index < ETH_RING_CAPACITY;
		 ++index)
		EthDma_PostRx(ring, index);
	return ETH_OK;
}

//复制发送数据并发布描述符所有权
EthResult_t eth_ring_tx(
	EthDmaRing_t *ring,
	const void *data,
	size_t length,
	uint32_t *ticket
)
{
	EthDmaDescriptor_t *descriptor;
	unsigned index;

	if (ring == NULL || ring->memory == NULL || data == NULL || ticket == NULL || length < 14U ||
		length > ETH_FRAME_MAX)
		return ETH_ARGUMENT;
	if (EthDma_MemoryOverlaps(ring, data, length) ||
		EthDma_MemoryOverlaps(ring, ticket, sizeof(*ticket)))
		return ETH_ARGUMENT;
	if (ring->tx_count == ETH_RING_CAPACITY)
	{
		++ring->statistics.tx_full;
		return ETH_BUSY;
	}
	index = ring->tx_head;
	descriptor = &ring->memory->tx[index];
	if (ring->pending[index] || (descriptor->w[3] & ETH_DESC_OWN) != 0U)
	{
		++ring->statistics.ownership_errors;
		return ETH_STATE;
	}
	EthDma_Barrier(ring);
	memcpy(ring->memory->tx_buffer[index], data, length);
	descriptor->w[0] = EthDma_Address(ring->memory->tx_buffer[index]);
	descriptor->w[1] = 0U;
	descriptor->w[2] = UINT32_C(0x80000000) | (uint32_t)length; //使能完成中断并填写缓冲区长度
	ring->pending[index] = 1U;
	ring->ticket[index] = ++ring->sequence;
	*ticket = ring->ticket[index];
	++ring->tx_count;
	ring->tx_head = EthDma_NextIndex(index);
	++ring->statistics.tx_submitted;
	EthDma_Barrier(ring);
	descriptor->w[3] = ETH_DESC_OWN | ETH_DESC_FD | ETH_DESC_LD | (uint32_t)length;
	EthDma_Barrier(ring);
	//尾指针指向下一个描述符，OWN 仍限制 DMA 访问
	ring->ops.tx_kick(ring->ops.context, EthDma_Address(&ring->memory->tx[ring->tx_head]));
	return ETH_OK;
}

//按提交顺序回收已完成的发送槽
EthResult_t eth_ring_reclaim(
	EthDmaRing_t *ring,
	uint32_t *ticket
)
{
	uint32_t status;
	unsigned index;
	EthResult_t result = ETH_OK;

	if (ring == NULL || ring->memory == NULL || ticket == NULL)
		return ETH_ARGUMENT;
	if (EthDma_MemoryOverlaps(ring, ticket, sizeof(*ticket)))
		return ETH_ARGUMENT;
	if (ring->tx_count == 0U)
		return ETH_EMPTY;
	index = ring->tx_tail;
	status = ring->memory->tx[index].w[3];
	if (!ring->pending[index])
	{
		++ring->statistics.ownership_errors;
		return ETH_STATE;
	}
	if ((status & ETH_DESC_OWN) != 0U)
		return ETH_EMPTY;
	EthDma_Barrier(ring);
	if ((status & (ETH_DESC_ES | UINT32_C(0x40000000))) != 0U ||
		(status & (ETH_DESC_FD | ETH_DESC_LD)) != (ETH_DESC_FD | ETH_DESC_LD))
	{
		++ring->statistics.tx_errors;
		result = ETH_FRAME_ERROR;
	}
	else
		++ring->statistics.tx_completed;
	*ticket = ring->ticket[index];
	ring->pending[index] = 0U;
	--ring->tx_count;
	ring->tx_tail = EthDma_NextIndex(index);
	return result;
}

//复制有效接收帧并轮转接收 guard
EthResult_t eth_ring_rx(
	EthDmaRing_t *ring,
	void *data,
	size_t capacity,
	size_t *length
)
{
	unsigned index;
	uint32_t status;
	size_t frame_length;
	EthResult_t result;
	if (ring == NULL || ring->memory == NULL || data == NULL || length == NULL)
		return ETH_ARGUMENT;
	if (EthDma_MemoryOverlaps(ring, data, capacity) ||
		EthDma_MemoryOverlaps(ring, length, sizeof(*length)))
		return ETH_ARGUMENT;
	*length = 0U;
	index = ring->rx_head;
	status = ring->memory->rx[index].w[3];
	if ((status & ETH_DESC_OWN) != 0U)
		return ETH_EMPTY;
	EthDma_Barrier(ring);
	frame_length = (size_t)(status & UINT32_C(0x7FFF));
	if ((status & ETH_DESC_FD) != 0U)
		ring->dropping = 0U;
	if (ring->dropping || (status & (ETH_DESC_FD | ETH_DESC_LD)) != (ETH_DESC_FD | ETH_DESC_LD) ||
		(status & (ETH_DESC_ES | UINT32_C(0x40000000))) != 0U || frame_length < 14U ||
		frame_length > ETH_FRAME_MAX)
	{
		++ring->statistics.rx_errors;
		result = ETH_FRAME_ERROR;
		ring->dropping = (uint8_t)((status & ETH_DESC_LD) == 0U);
	}
	else
	{
		*length = frame_length;
		if (capacity < frame_length)
		{
			++ring->statistics.rx_small;
			return ETH_CAPACITY;
		}
		memcpy(data, ring->memory->rx_buffer[index], frame_length);
		++ring->statistics.rx_delivered;
		result = ETH_OK;
	}
	EthDma_PostRx(ring, ring->rx_guard);
	ring->rx_guard = index;
	ring->rx_head = EthDma_NextIndex(index);
	//尾指针指向新 guard，归还旧 guard 的接收容量
	ring->ops.rx_kick(ring->ops.context, EthDma_Address(&ring->memory->rx[ring->rx_guard]));
	return result;
}

//内部功能函数实现

//转换硬件使用的 32 位地址
static uint32_t EthDma_Address(const void *pointer)
{
	return (uint32_t)(uintptr_t)pointer;
}

//推进环索引并处理回绕
static unsigned EthDma_NextIndex(unsigned index)
{
	return (index + 1U) % ETH_RING_SIZE;
}

//检查调用者内存是否与 DMA 对象重叠
static int EthDma_MemoryOverlaps(
	const EthDmaRing_t *ring,
	const void *pointer,
	size_t length
)
{
	uintptr_t caller_address = (uintptr_t)pointer;
	uintptr_t dma_address = (uintptr_t)ring->memory;

	return caller_address <= dma_address ? dma_address - caller_address < length
										 : caller_address - dma_address < sizeof(*ring->memory);
}

//保证数据、OWN 和尾指针的发布顺序
static void EthDma_Barrier(EthDmaRing_t *ring)
{
	ring->ops.barrier(ring->ops.context);
}

//重备接收地址并把描述符发布给 DMA
static void EthDma_PostRx(
	EthDmaRing_t *ring,
	unsigned index
)
{
	EthDmaDescriptor_t *descriptor = &ring->memory->rx[index];

	descriptor->w[0] = EthDma_Address(ring->memory->rx_buffer[index]);
	descriptor->w[1] = 0U;
	descriptor->w[2] = 0U;
	EthDma_Barrier(ring);
	descriptor->w[3] = ETH_RX_POST;
	EthDma_Barrier(ring);
}
