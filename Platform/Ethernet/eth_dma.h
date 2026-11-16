#ifndef ETH_DMA_H
#define ETH_DMA_H
#include <stdint.h>
#include <stddef.h>
#define ETH_RING_SIZE 4U
#define ETH_RING_CAPACITY (ETH_RING_SIZE-1U) /* one exclusive-tail guard */
#define ETH_BUFFER_SIZE 1536U
#define ETH_FRAME_MAX 1522U
#define ETH_DESC_OWN UINT32_C(0x80000000)
#define ETH_DESC_FD UINT32_C(0x20000000)
#define ETH_DESC_LD UINT32_C(0x10000000)
#define ETH_DESC_ES UINT32_C(0x00008000)
#define ETH_RX_POST UINT32_C(0xC1000000) /* OWN, IOC, BUF1V */
/* 16 hardware bytes + 16 skipped bytes; DMACCR.DSL=4, 32-byte stride. */
typedef struct { volatile uint32_t w[4]; uint32_t skip[4]; } eth_descriptor;
typedef struct {
    eth_descriptor tx[ETH_RING_SIZE], rx[ETH_RING_SIZE];
    uint8_t tx_buffer[ETH_RING_SIZE][ETH_BUFFER_SIZE];
    uint8_t rx_buffer[ETH_RING_SIZE][ETH_BUFFER_SIZE];
} eth_dma_memory;
typedef enum { ETH_OK=0, ETH_EMPTY=1, ETH_BUSY=2, ETH_LINK_DOWN=3,
    ETH_ARGUMENT=-1, ETH_IO=-2, ETH_STATE=-3, ETH_FRAME_ERROR=-4,
    ETH_CAPACITY=-5, ETH_TIMEOUT=-6, ETH_MEMORY=-7, ETH_PHY_MISSING=-8 } eth_result;
typedef struct {
    uint32_t tx_submitted, tx_completed, tx_errors, tx_full, tx_aborted;
    uint32_t rx_delivered, rx_errors, rx_small, ownership_errors;
} eth_ring_statistics;
typedef struct {
    void *context;
    void (*barrier)(void *);
    void (*tx_kick)(void *, uint32_t);
    void (*rx_kick)(void *, uint32_t);
} eth_dma_ops;
typedef struct {
    eth_dma_memory *memory;
    eth_dma_ops ops;
    unsigned tx_head, tx_tail, tx_count, rx_head, rx_guard;
    uint8_t pending[ETH_RING_SIZE], dropping;
    uint32_t sequence, ticket[ETH_RING_SIZE];
    eth_ring_statistics statistics;
} eth_ring;
/* Internal reset API: caller MUST first positively quiesce/reset hardware.
 * Memory must be 32-byte aligned, wholly DMA-accessible and noncacheable.
 * Caller serializes all ring APIs. No ring functions may run in the ISR. */
eth_result eth_ring_reset(eth_ring *, eth_dma_memory *, const eth_dma_ops *);
/* Copies input; caller retains original. Success returns a unique completion
 * ticket. Internal copy remains unavailable until reclaim or hardware reset. */
eth_result eth_ring_tx(eth_ring *, const void *, size_t, uint32_t *ticket);
/* One ordered completion per call; EMPTY includes still-owned head.
 * ticket is valid for OK or FRAME_ERROR, no other result. */
eth_result eth_ring_reclaim(eth_ring *, uint32_t *ticket);
/* Copy-mode RX; no DMA pointer escapes. CAPACITY reports required length and
 * leaves descriptor pending. Errors drop/repost exactly the consumed slot. */
eth_result eth_ring_rx(eth_ring *, void *, size_t, size_t *length);
#endif
