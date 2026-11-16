#include "eth_phy.h"
#include <stddef.h>
static int valid(const eth_phy *p)
{
    return p != NULL && p->read != NULL && p->write != NULL && p->address < 32U;
}
eth_phy_result eth_phy_probe(const eth_phy *p, uint32_t *id)
{
    uint16_t a, b;
    if (!valid(p) || id == NULL) return ETH_PHY_ARGUMENT;
    *id = 0U;
    if (p->read(p->context,p->address,2U,&a) != 0 ||
        p->read(p->context,p->address,3U,&b) != 0) return ETH_PHY_IO;
    *id = ((uint32_t)a << 16) | b;
    /* LAN8742A OUI/model, accepting any silicon revision. */
    return ((*id & 0xFFFFFFF0UL) == 0x0007C130UL) ? ETH_PHY_OK : ETH_PHY_ID;
}
eth_phy_result eth_phy_reset_begin(const eth_phy *p)
{
    if (!valid(p)) return ETH_PHY_ARGUMENT;
    return p->write(p->context,p->address,0U,0x8000U) == 0 ? ETH_PHY_OK : ETH_PHY_IO;
}
eth_phy_result eth_phy_reset_poll(const eth_phy *p)
{
    uint16_t value;
    if (!valid(p)) return ETH_PHY_ARGUMENT;
    if (p->read(p->context,p->address,0U,&value) != 0) return ETH_PHY_IO;
    return (value & 0x8000U) != 0U ? ETH_PHY_WAIT : ETH_PHY_OK;
}
eth_phy_result eth_phy_read_link(const eth_phy *p, eth_phy_link *link)
{
    uint16_t value;
    if (!valid(p) || link == NULL) return ETH_PHY_ARGUMENT;
    link->up = 0U;
    link->autoneg_complete = 0U;
    if (p->read(p->context,p->address,1U,&value) != 0 ||
        p->read(p->context,p->address,1U,&value) != 0) return ETH_PHY_IO;
    link->up = (uint8_t)((value & 4U) != 0U);
    link->autoneg_complete = (uint8_t)((value & 32U) != 0U);
    return ETH_PHY_OK;
}
