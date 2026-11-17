#include "eth_dma.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#if defined(_MSC_VER)
__declspec(align(32)) static eth_dma_memory memory;
#else
static eth_dma_memory memory __attribute__((aligned(32)));
#endif
static eth_ring r;
static unsigned barriers, tx_kicks, rx_kicks;
static uint32_t tx_tail, rx_tail;
static void barrier(void *c)
{
	(void)c;
	++barriers;
}
static void tx(void *c, uint32_t t)
{
	(void)c;
	++tx_kicks;
	tx_tail = t;
}
static void rx(void *c, uint32_t t)
{
	(void)c;
	++rx_kicks;
	rx_tail = t;
}
static const eth_dma_ops ops = {NULL, barrier, tx, rx};
static uint32_t addr(const void *p)
{
	return (uint32_t)(uintptr_t)p;
}
static void fresh(void)
{
	assert(eth_ring_reset(&r, &memory, &ops) == ETH_OK);
}
int main(void)
{
	uint8_t frame[ETH_FRAME_MAX], out[ETH_BUFFER_SIZE];
	uint32_t ticket, done, ids[ETH_RING_CAPACITY];
	size_t n;
	unsigned i, round;
	memset(frame, 0xA5, sizeof(frame));
	assert(eth_ring_reset(NULL, &memory, &ops) == ETH_ARGUMENT);
	fresh();
	assert(sizeof(eth_descriptor) == 32U);
	for (i = 0; i < ETH_RING_SIZE; ++i)
	{
		assert(memory.tx[i].w[3] == 0U);
		assert(memory.rx[i].w[3] == (i < ETH_RING_CAPACITY ? ETH_RX_POST : 0U));
	}
	assert(eth_ring_reclaim(&r, &done) == ETH_EMPTY);
	assert(eth_ring_rx(&r, out, sizeof(out), &n) == ETH_EMPTY && n == 0U);
	assert(eth_ring_tx(&r, frame, 13U, &ticket) == ETH_ARGUMENT);
	assert(eth_ring_tx(&r, memory.tx_buffer[0], 60U, &ticket) == ETH_ARGUMENT);
	assert(eth_ring_rx(&r, memory.rx_buffer[1], 60U, &n) == ETH_ARGUMENT);
	assert(eth_ring_tx(&r, frame, ETH_FRAME_MAX + 1U, &ticket) == ETH_ARGUMENT);
	for (i = 0; i < ETH_RING_CAPACITY; ++i)
	{
		assert(eth_ring_tx(&r, frame, sizeof(frame), &ids[i]) == ETH_OK);
		assert(memory.tx[i].w[3] & ETH_DESC_OWN);
		assert(memory.tx[i].w[2] == (0x80000000U | ETH_FRAME_MAX));
		assert(memcmp(memory.tx_buffer[i], frame, sizeof(frame)) == 0);
	}
	assert(tx_tail == addr(&memory.tx[ETH_RING_CAPACITY]));
	assert(eth_ring_tx(&r, frame, 60U, &ticket) == ETH_BUSY);
	assert(eth_ring_reclaim(&r, &done) == ETH_EMPTY);
	/* Out-of-order hardware completion must not skip the owned head. */
	memory.tx[1].w[3] = ETH_DESC_FD | ETH_DESC_LD;
	assert(eth_ring_reclaim(&r, &done) == ETH_EMPTY);
	memory.tx[0].w[3] = ETH_DESC_FD | ETH_DESC_LD;
	assert(eth_ring_reclaim(&r, &done) == ETH_OK && done == ids[0]);
	assert(eth_ring_reclaim(&r, &done) == ETH_OK && done == ids[1]);
	memory.tx[2].w[3] = ETH_DESC_ES | ETH_DESC_FD | ETH_DESC_LD;
	assert(eth_ring_reclaim(&r, &done) == ETH_FRAME_ERROR && done == ids[2]);
	assert(eth_ring_reclaim(&r, &done) == ETH_EMPTY);
	for (round = 0; round < 1000U; ++round)
	{
		i = r.tx_head;
		assert(eth_ring_tx(&r, frame, 60U, &ticket) == ETH_OK);
		assert(eth_ring_reclaim(&r, &done) == ETH_EMPTY);
		memory.tx[i].w[3] = ETH_DESC_FD | ETH_DESC_LD;
		assert(eth_ring_reclaim(&r, &done) == ETH_OK && done == ticket);
	}
	/* Illegal OWN on a nominally free descriptor is never overwritten. */
	i = r.tx_head;
	memory.tx[i].w[3] = ETH_DESC_OWN;
	assert(eth_ring_tx(&r, frame, 60U, &ticket) == ETH_STATE);
	assert(memory.tx[i].w[3] == ETH_DESC_OWN);
	fresh();
	assert(eth_ring_tx(&r, frame, 60U, &ticket) == ETH_OK);
	r.pending[0] = 0U;
	memory.tx[0].w[3] = 0U;
	assert(eth_ring_reclaim(&r, &done) == ETH_STATE);
	fresh();
	assert(eth_ring_tx(&r, frame, 60U, &ticket) == ETH_OK);
	memory.tx[0].w[3] = 0U;
	assert(eth_ring_reclaim(&r, &done) == ETH_FRAME_ERROR);
	fresh();
	for (round = 0; round < 1000U; ++round)
	{
		unsigned guard = r.rx_guard;
		i = r.rx_head;
		assert(memory.rx[i].w[3] & ETH_DESC_OWN);
		memcpy(memory.rx_buffer[i], frame, 60U);
		memory.rx[i].w[3] = ETH_DESC_FD | ETH_DESC_LD | 60U;
		assert(eth_ring_rx(&r, out, 59U, &n) == ETH_CAPACITY && n == 60U);
		assert((memory.rx[i].w[3] & ETH_DESC_OWN) == 0U);
		assert(eth_ring_rx(&r, out, sizeof(out), &n) == ETH_OK && n == 60U);
		assert(memcmp(out, frame, n) == 0);
		assert((memory.rx[i].w[3] & ETH_DESC_OWN) == 0U); /* new guard */
		assert(memory.rx[guard].w[3] == ETH_RX_POST);
		assert(rx_tail == addr(&memory.rx[i]));
	}
	fresh();
	memory.rx[0].w[3] = ETH_DESC_FD | ETH_DESC_LD | ETH_DESC_ES | 60U;
	assert(eth_ring_rx(&r, out, sizeof(out), &n) == ETH_FRAME_ERROR);
	memory.rx[1].w[3] = ETH_DESC_FD | 60U; /* fragmented frame */
	assert(eth_ring_rx(&r, out, sizeof(out), &n) == ETH_FRAME_ERROR);
	memory.rx[2].w[3] = ETH_DESC_LD | 60U;
	assert(eth_ring_rx(&r, out, sizeof(out), &n) == ETH_FRAME_ERROR);
	memory.rx[3].w[3] = ETH_DESC_FD | ETH_DESC_LD | 1600U;
	assert(eth_ring_rx(&r, out, sizeof(out), &n) == ETH_FRAME_ERROR);
	assert(r.statistics.rx_errors == 4U);
	assert(barriers > 0U && tx_kicks > 0U && rx_kicks > 0U);
	puts("PASS: DMA init, OWN, guards, full/empty, copy, reclaim, errors, 1000 wraps");
	return 0;
}
