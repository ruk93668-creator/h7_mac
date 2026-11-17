/* Independent cursor model: fetch only before exclusive tail, with OWN set. */
#include "eth_dma.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
__declspec(align(32)) static eth_dma_memory memory;
static eth_ring ring;
static unsigned tc, rc, tb, rb;
static void fence(void *c)
{
	(void)c;
}
static unsigned index_for(uint32_t a, const eth_descriptor *base)
{
	unsigned i;
	for (i = 0; i < ETH_RING_SIZE; ++i)
		if ((uint32_t)(uintptr_t)&base[i] == a)
			return i;
	assert(0);
	return 0;
}
static void tx_tail(void *c, uint32_t t)
{
	(void)c;
	tb = index_for(t, memory.tx);
}
static void rx_tail(void *c, uint32_t t)
{
	(void)c;
	rb = index_for(t, memory.rx);
}
static unsigned transmit(void)
{
	unsigned n = 0;
	while (tc != tb && (memory.tx[tc].w[3] & ETH_DESC_OWN) != 0U)
	{
		assert(memory.tx[tc].w[0] == (uint32_t)(uintptr_t)memory.tx_buffer[tc]);
		memory.tx[tc].w[3] = ETH_DESC_FD | ETH_DESC_LD;
		tc = (tc + 1U) % ETH_RING_SIZE;
		++n;
	}
	return n;
}
static int receive(uint8_t v)
{
	if (rc == rb || (memory.rx[rc].w[3] & ETH_DESC_OWN) == 0U)
		return 0;
	assert(memory.rx[rc].w[0] == (uint32_t)(uintptr_t)memory.rx_buffer[rc]);
	memset(memory.rx_buffer[rc], v, 60U);
	memory.rx[rc].w[3] = ETH_DESC_FD | ETH_DESC_LD | 60U;
	rc = (rc + 1U) % ETH_RING_SIZE;
	return 1;
}
int main(void)
{
	const eth_dma_ops ops = {NULL, fence, tx_tail, rx_tail};
	uint8_t frame[60] = {0}, out[60];
	unsigned round, i;
	uint32_t ticket;
	size_t n;
	assert(eth_ring_reset(&ring, &memory, &ops) == ETH_OK);
	rb = ETH_RING_CAPACITY;
	for (round = 0; round < 1000U; ++round)
	{
		for (i = 0; i < ETH_RING_CAPACITY; ++i)
			assert(eth_ring_tx(&ring, frame, sizeof(frame), &ticket) == ETH_OK);
		assert(eth_ring_tx(&ring, frame, sizeof(frame), &ticket) == ETH_BUSY);
		assert(transmit() == ETH_RING_CAPACITY && transmit() == 0U);
		for (i = 0; i < ETH_RING_CAPACITY; ++i)
			assert(eth_ring_reclaim(&ring, &ticket) == ETH_OK);
		assert(eth_ring_reclaim(&ring, &ticket) == ETH_EMPTY);
		for (i = 0; i < ETH_RING_CAPACITY; ++i)
			assert(receive((uint8_t)i));
		assert(!receive(0xFFU));
		for (i = 0; i < ETH_RING_CAPACITY; ++i)
		{
			assert(eth_ring_rx(&ring, out, sizeof(out), &n) == ETH_OK && n == 60U);
			assert(out[0] == (uint8_t)i);
		}
		assert(eth_ring_rx(&ring, out, sizeof(out), &n) == ETH_EMPTY);
	}
	assert(ring.statistics.tx_completed == 3000U && ring.statistics.rx_delivered == 3000U);
	puts("PASS: independent DMA cursor/tail model, exhaustion/resume, 3000 frames each");
	return 0;
}
