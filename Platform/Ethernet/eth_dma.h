//描述符环和内部缓冲区管理
#ifndef ETH_DMA_H
#define ETH_DMA_H

#include "eth_types.h"
#include <stddef.h>
#define ETH_RING_SIZE 4U
#define ETH_RING_CAPACITY (ETH_RING_SIZE - 1U) //保留一个尾指针 guard
#define ETH_BUFFER_SIZE 1536U
#define ETH_FRAME_MAX 1522U
#define ETH_DESC_OWN UINT32_C(0x80000000)
#define ETH_DESC_FD UINT32_C(0x20000000)
#define ETH_DESC_LD UINT32_C(0x10000000)
#define ETH_DESC_ES UINT32_C(0x00008000)
#define ETH_RX_POST UINT32_C(0xC1000000) //DMA 所有权、完成中断和缓冲区有效位
//硬件字段与跳过字段各 16 字节，DSL=4，描述符步长 32 字节
typedef struct
{
	//硬件读写的四个描述符字
	volatile uint32_t w[4];
	//匹配 DSL 配置的跳过区域
	uint32_t skip[4];
} EthDmaDescriptor_t;
typedef struct
{
	//物理发送描述符数组
	EthDmaDescriptor_t tx[ETH_RING_SIZE];
	//物理接收描述符数组
	EthDmaDescriptor_t rx[ETH_RING_SIZE];
	//描述符对应的内部发送副本
	uint8_t tx_buffer[ETH_RING_SIZE][ETH_BUFFER_SIZE];
	//描述符对应的内部接收空间
	uint8_t rx_buffer[ETH_RING_SIZE][ETH_BUFFER_SIZE];
} EthDmaMemory_t;

typedef struct
{
	//回调上下文，由调用者管理
	void *context;
	void (*barrier)(void *context);
	void (*tx_kick)(void *context, uint32_t tail);
	void (*rx_kick)(void *context, uint32_t tail);
} EthDmaOps_t;
typedef struct
{
	//专用 DMA 内存池
	EthDmaMemory_t *memory;
	//硬件屏障及尾指针适配
	EthDmaOps_t ops;
	//下一次提交位置
	unsigned tx_head;
	//下一次回收位置
	unsigned tx_tail;
	//尚未回收的发送槽数
	unsigned tx_count;
	//下一次消费位置
	unsigned rx_head;
	//限制 DMA 处理范围的保留槽
	unsigned rx_guard;
	//软件占用标志，清 OWN 后仍须回收
	uint8_t pending[ETH_RING_SIZE];
	//跨描述符错误帧丢弃状态
	uint8_t dropping;
	//发送 ticket 序号
	uint32_t sequence;
	//每个待回收槽的完成标识
	uint32_t ticket[ETH_RING_SIZE];
	//软件环计数
	EthRingStatistics_t statistics;
} EthDmaRing_t;

//仅在硬件停止并确认复位后重建；内存须 32 字节对齐、DMA 可达且非缓存
//调用者串行使用，ISR 不操作软件环
EthResult_t eth_ring_reset(
	EthDmaRing_t *ring,
	EthDmaMemory_t *memory,
	const EthDmaOps_t *ops
);

//同步复制输入，调用者保留原始数据；成功取得完成 ticket
//内部副本直到完成回收或硬件复位后才能复用
EthResult_t eth_ring_tx(
	EthDmaRing_t *ring,
	const void *data,
	size_t length,
	uint32_t *ticket
);

//每次按序回收一个完成；空环或头部仍归 DMA 所有时返回 EMPTY
//ticket 仅在 OK 或 FRAME_ERROR 时有效
EthResult_t eth_ring_reclaim(
	EthDmaRing_t *ring,
	uint32_t *ticket
);

//接收数据复制给调用者，不交出 DMA 指针
//容量不足时保留帧并报告长度；错误帧消费后归还容量
EthResult_t eth_ring_rx(
	EthDmaRing_t *ring,
	void *data,
	size_t capacity,
	size_t *length
);

//保留已有类型名，避免破坏现有 PHY 和收发调用接口
typedef EthDmaDescriptor_t eth_descriptor;
typedef EthDmaMemory_t eth_dma_memory;
typedef EthDmaOps_t eth_dma_ops;
typedef EthDmaRing_t eth_ring;

#endif
