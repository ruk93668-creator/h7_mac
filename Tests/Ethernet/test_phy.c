#include "eth_phy.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
static uint16_t registers[32];
static uint16_t first_bsr;
static unsigned reads;
static unsigned fail_at;
static unsigned writes;
static int read_reg(
	void *c,
	uint8_t a,
	uint8_t r,
	uint16_t *v
)
{
	(void)c;
	assert(a == 0U);
	++reads;
	if (reads == fail_at)
		return -1;
	*v = r == 1U && reads == 1U ? first_bsr : registers[r];
	return 0;
}
static int write_reg(
	void *c,
	uint8_t a,
	uint8_t r,
	uint16_t v
)
{
	(void)c;
	assert(a == 0U);
	++writes;
	if (fail_at != 0U)
		return -1;
	registers[r] = v;
	return 0;
}
int main(void)
{
	EthPhy_t p = {NULL, read_reg, write_reg, 0U};
	EthPhyLink_t link;
	uint32_t id;

	assert(eth_phy_probe(NULL, &id) == ETH_PHY_ARGUMENT);
	p.address = 32U;
	assert(eth_phy_reset_begin(&p) == ETH_PHY_ARGUMENT);
	p.address = 0U;
	registers[2] = 7U;
	registers[3] = 0xC13FU;
	assert(eth_phy_probe(&p, &id) == ETH_PHY_OK && id == 0x0007C13FUL);
	registers[2] = 0xFFFFU;
	registers[3] = 0xFFFFU;
	assert(eth_phy_probe(&p, &id) == ETH_PHY_ID);
	registers[2] = 0U;
	registers[3] = 0U;
	assert(eth_phy_probe(&p, &id) == ETH_PHY_ID);
	reads = 0U;
	fail_at = 2U;
	assert(eth_phy_probe(&p, &id) == ETH_PHY_IO && id == 0U);
	fail_at = 0U;
	assert(eth_phy_reset_begin(&p) == ETH_PHY_OK && writes == 1U);
	assert(eth_phy_reset_poll(&p) == ETH_PHY_WAIT);
	registers[0] = 0U;
	assert(eth_phy_reset_poll(&p) == ETH_PHY_OK);
	fail_at = 1U;
	assert(eth_phy_reset_begin(&p) == ETH_PHY_IO);
	reads = 0U;
	assert(eth_phy_reset_poll(&p) == ETH_PHY_IO);
	fail_at = 0U;
	reads = 0U;
	first_bsr = 0U;
	registers[1] = 36U;
	assert(eth_phy_read_link(&p, &link) == ETH_PHY_OK && reads == 2U);
	assert(link.up == 1U && link.autoneg_complete == 1U);
	reads = 0U;
	first_bsr = 36U;
	registers[1] = 0U;
	assert(eth_phy_read_link(&p, &link) == ETH_PHY_OK && link.up == 0U);
	reads = 0U;
	fail_at = 2U;
	link.up = 1U;
	assert(eth_phy_read_link(&p, &link) == ETH_PHY_IO && link.up == 0U);
	p.read = NULL;
	assert(eth_phy_read_link(&p, &link) == ETH_PHY_ARGUMENT);
	puts("PASS: PHY identity, arguments, reset, latch-low and MDIO failures");
	return 0;
}
