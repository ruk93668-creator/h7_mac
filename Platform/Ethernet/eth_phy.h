#ifndef ETH_PHY_H
#define ETH_PHY_H
#include <stdint.h>
/* Callbacks return 0 on success; must have bounded MDIO completion times.
 * Caller serializes all operations, including reset/link polling. */
typedef struct {
    void *context;
    int (*read)(void *, uint8_t, uint8_t, uint16_t *);
    int (*write)(void *, uint8_t, uint8_t, uint16_t);
    uint8_t address;
} eth_phy;
typedef enum { ETH_PHY_OK=0, ETH_PHY_IO=-1, ETH_PHY_ARGUMENT=-2,
    ETH_PHY_ID=-3, ETH_PHY_WAIT=1 } eth_phy_result;
typedef struct { uint8_t up, autoneg_complete; } eth_phy_link;
/* Probe only the board-selected address, never silently select another PHY. */
eth_phy_result eth_phy_probe(const eth_phy *phy, uint32_t *id);
/* Software reset only; board code handles nRST timing and strap sampling. */
eth_phy_result eth_phy_reset_begin(const eth_phy *phy);
eth_phy_result eth_phy_reset_poll(const eth_phy *phy);
/* Reads BSR twice to clear latch-low. Does not infer negotiated speed/duplex. */
eth_phy_result eth_phy_read_link(const eth_phy *phy, eth_phy_link *link);
#endif
