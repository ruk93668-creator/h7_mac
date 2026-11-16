#ifndef ETH_MAC_H
#define ETH_MAC_H
#include "eth_dma.h"
#include "eth_phy.h"
typedef enum { ETH_MAC_OFF=0, ETH_MAC_READY, ETH_MAC_RUNNING, ETH_MAC_FAULT } eth_mac_state;
typedef struct { uint8_t up, autoneg_complete, full_duplex; uint16_t speed_mbps; } eth_link_state;
typedef struct {
    eth_mac_state state;
    eth_ring_statistics ring;
    uint32_t irq_tx, irq_rx, irq_tbu, irq_rbu, irq_dma_error, irq_mac_error;
    uint32_t last_dma_status, last_mac_status, init_errors, start_errors, mdio_errors;
    uint32_t phy_id;
} eth_statistics;
/* Single task context only, externally serialized. HAL millisecond tick must
 * run. No RTOS APIs. Init is explicit, never auto-registered with Application.
 * Requires board 50 MHz REF_CLK and the dedicated MPU/linker configuration. */
eth_result eth_mac_init(const uint8_t mac[6]);
eth_result eth_mac_start(void);
/* Abortive bounded stop resets MAC/DMA before returning internal buffers.
 * Pending tickets are aborted, not successful TX completions. */
eth_result eth_mac_stop(void);
eth_result eth_mac_get_link(eth_link_state *);
eth_result eth_mac_get_address(uint8_t mac[6]);
eth_result eth_mac_get_statistics(eth_statistics *);
eth_result eth_mac_tx_submit(const void *, size_t, uint32_t *ticket);
eth_result eth_mac_tx_reclaim(uint32_t *ticket);
eth_result eth_mac_rx_poll(void *, size_t, size_t *length);
/* Call periodically from the SAME task: link loss/mode change stops DMA.
 * Fault recovery requires explicit stop and start; never auto-retry TX. */
eth_result eth_mac_service(void);
/* Standalone IRQ implementation; no FreeRTOS calls or descriptor processing. */
void ETH_IRQHandler(void);
#endif
