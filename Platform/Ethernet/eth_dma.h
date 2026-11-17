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
	volatile uint32_t w[4];
	uint32_t skip[4];
} eth_descriptor;
typedef struct
{
	eth_descriptor tx[ETH_RING_SIZE], rx[ETH_RING_SIZE];
	uint8_t tx_buffer[ETH_RING_SIZE][ETH_BUFFER_SIZE];
	uint8_t rx_buffer[ETH_RING_SIZE][ETH_BUFFER_SIZE];
} eth_dma_memory;

typedef struct
{
	void *context;
	void (*barrier)(void *);
	void (*tx_kick)(void *, uint32_t);
	void (*rx_kick)(void *, uint32_t);
} eth_dma_ops;
typedef struct
{
	eth_dma_memory *memory;
	eth_dma_ops ops;
	unsigned tx_head, tx_tail, tx_count, rx_head, rx_guard;
	uint8_t pending[ETH_RING_SIZE], dropping;
	uint32_t sequence, ticket[ETH_RING_SIZE];
	eth_ring_statistics statistics;
} eth_ring;
//仅在硬件停止并确认复位后重建；内存须 32 字节对齐、DMA 可达且非缓存
//调用者串行使用，ISR 不操作软件环
eth_result eth_ring_reset(eth_ring *, eth_dma_memory *, const eth_dma_ops *);
//同步复制输入，调用者保留原始数据；成功取得完成 ticket
//内部副本直到完成回收或硬件复位后才能复用
eth_result eth_ring_tx(eth_ring *, const void *, size_t, uint32_t *ticket);
//每次按序回收一个完成；空环或头部仍归 DMA 所有时返回 EMPTY
//ticket 仅在 OK 或 FRAME_ERROR 时有效
eth_result eth_ring_reclaim(eth_ring *, uint32_t *ticket);
//接收数据复制给调用者，不交出 DMA 指针
//容量不足时保留帧并报告长度；错误帧消费后归还容量
eth_result eth_ring_rx(eth_ring *, void *, size_t, size_t *length);
#endif
