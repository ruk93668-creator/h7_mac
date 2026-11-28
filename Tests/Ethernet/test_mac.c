#include "eth_mac.h"
#include "eth_memory.h"
#include "fake_hal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
FakeEth fake_eth;
FakeRcc fake_rcc;
FakeSyscfg fake_syscfg;
__declspec(align(32)) EthDmaMemory_t g_eth_dma_memory;
static uint16_t phy_regs[32];
static uint32_t tick;
static uint32_t hclk = 240000000U;
static unsigned fail_reset;
static unsigned fail_mdio;
static unsigned fail_flush;
static unsigned valid_memory = 1U;
static unsigned irq_enabled;
int eth_memory_valid(void)
{
	return (int)valid_memory;
}
void eth_memory_configure(void)
{
}
void fake_reset(void)
{
	memset(&fake_eth, 0, sizeof(fake_eth));
}
uint32_t HAL_GetTick(void)
{
	++tick;
	if (!fail_reset)
		fake_eth.DMAMR &= ~ETH_DMAMR_SWR;
	if (!fail_flush)
		fake_eth.MTLTQOMR &= ~ETH_MTLTQOMR_FTQ;
	if (!fail_mdio && (fake_eth.MACMDIOAR & ETH_MACMDIOAR_MB) != 0U)
	{
		unsigned a = (fake_eth.MACMDIOAR >> 21) & 31U, r = (fake_eth.MACMDIOAR >> 16) & 31U;
		assert(a == 0U);
		if ((fake_eth.MACMDIOAR & 0xCU) == ETH_MACMDIOAR_MOC_WR)
			phy_regs[r] = (uint16_t)fake_eth.MACMDIODR;
		else
			fake_eth.MACMDIODR = phy_regs[r];
		fake_eth.MACMDIOAR &= ~ETH_MACMDIOAR_MB;
	}
	return tick;
}
uint32_t HAL_RCC_GetHCLKFreq(void)
{
	return hclk;
}
void HAL_Delay(uint32_t n)
{
	tick += n;
}
void HAL_GPIO_WritePin(
	void *p,
	uint32_t pin,
	uint32_t v
)
{
	(void)p;
	(void)v;
	assert(pin == GPIO_PIN_9);
}
void HAL_GPIO_Init(
	void *p,
	GPIO_InitTypeDef *g
)
{
	(void)p;
	assert(g->Pull == GPIO_NOPULL);
}
void HAL_SYSCFG_ETHInterfaceSelect(uint32_t v)
{
	assert(v == SYSCFG_ETH_RMII);
}
void HAL_NVIC_DisableIRQ(int n)
{
	assert(n == ETH_IRQn);
	irq_enabled = 0U;
}
void HAL_NVIC_EnableIRQ(int n)
{
	assert(n == ETH_IRQn);
	irq_enabled = 1U;
}
void HAL_NVIC_ClearPendingIRQ(int n)
{
	assert(n == ETH_IRQn);
}
int main(
	int argc,
	char **argv
)
{
	const uint8_t mac[6] = {2U, 0x47U, 0x47U, 0U, 0U, 1U};
	uint8_t frame[60] = {0};
	uint8_t copy[6];
	uint32_t ticket;
	uint32_t done;
	EthStatistics_t s;
	EthLinkState_t link;

	if (argc == 4 && strcmp(argv[1], "clock") == 0)
	{
		hclk = (uint32_t)strtoul(argv[2], NULL, 10);
		phy_regs[2] = 7U;
		phy_regs[3] = 0xC130U;
		assert(eth_mac_init(mac) == ETH_OK);
		assert((fake_eth.MACMDIOAR & 0xF00U) == (uint32_t)strtoul(argv[3], NULL, 10));
		return 0;
	}
	if (argc > 1 && strcmp(argv[1], "mdio_timeout") != 0)
	{
		if (strcmp(argv[1], "init_reset") == 0)
		{
			fail_reset = 1U;
			assert(eth_mac_init(mac) == ETH_TIMEOUT);
		}
		else
		{
			assert(strcmp(argv[1], "init_missing") == 0);
			assert(eth_mac_init(mac) == ETH_PHY_MISSING);
		}
		assert(eth_mac_get_statistics(&s) == ETH_OK && s.state == ETH_MAC_FAULT &&
			s.init_errors == 1U);
		fail_reset = 0U;
        phy_regs[2] = 7U;
        phy_regs[3] = 0xC130U;
        assert(eth_mac_recover(mac) == ETH_OK);
        assert(eth_mac_get_statistics(&s) == ETH_OK && s.state == ETH_MAC_READY);
        fail_reset = 1U;
        assert(eth_mac_recover(mac) == ETH_TIMEOUT);
        assert(eth_mac_get_statistics(&s) == ETH_OK && s.state == ETH_MAC_FAULT);
        fail_reset = 0U;
        assert(eth_mac_recover(mac) == ETH_OK);
		puts("PASS: MAC initialization failure is reported and latched");
		return 0;
	}
	if (argc == 2 && strcmp(argv[1], "mdio_timeout") == 0)
	{
		EthMdioFault_t first;
		phy_regs[2] = 7U;
		phy_regs[3] = 0xC130U;
		assert(eth_mac_init(mac) == ETH_OK);
		fail_mdio = 1U;
		assert(eth_mac_start() == ETH_MDIO);
		assert(eth_mac_get_statistics(&s) == ETH_OK && s.mdio_wait_timeouts == 1U);
		first = s.first_mdio_fault;
		assert(first.stage == ETH_MDIO_WAIT_TIMEOUT && first.phy_register == 2U &&
			first.phy_address == 0U && first.write == 0U && first.tick_ms != 0U);
		assert(first.macmdioar == fake_eth.MACMDIOAR && first.macmdiodr == fake_eth.MACMDIODR);
		assert(first.hclk_hz == hclk && first.mdc_clock_range == ETH_MACMDIOAR_CR_DIV102);
		assert(eth_mac_start() == ETH_MDIO);
		fail_mdio = 0U;
		assert(eth_mac_recover(mac) == ETH_OK);
		assert(eth_mac_get_statistics(&s) == ETH_OK && s.mdio_busy_on_entry == 1U &&
			s.phy_register_read_errors == 2U &&
			memcmp(&first, &s.first_mdio_fault, sizeof(first)) == 0);
		puts("PASS: MDIO timeout first snapshot survives subsequent busy and peripheral reset");
		return 0;
	}
	assert(eth_mac_start() == ETH_STATE);
	assert(eth_mac_init(NULL) == ETH_ARGUMENT);
	valid_memory = 0U;
	assert(eth_mac_init(mac) == ETH_MEMORY);
	valid_memory = 1U;
	phy_regs[2] = 7U;
	phy_regs[3] = 0xC130U;
	assert(eth_mac_init(mac) == ETH_OK);
	assert(fake_eth.MACCR == (ETH_MACCR_CST | ETH_MACCR_ACS));
	assert((fake_eth.MACMDIOAR & 0xF00U) == ETH_MACMDIOAR_CR_DIV102);
	assert(eth_mac_init(mac) == ETH_STATE);
	assert(eth_mac_get_address(copy) == ETH_OK && memcmp(copy, mac, 6U) == 0);
	assert(eth_mac_start() == ETH_LINK_DOWN);
	phy_regs[1] = 36U;
	phy_regs[0] = 0x1000U;
	phy_regs[31] = 0x1018U;
	assert(eth_mac_get_link(&link) == ETH_OK && link.up && link.speed_mbps == 100U &&
		link.full_duplex);
	assert(eth_mac_start() == ETH_OK && irq_enabled);
    assert(eth_mac_recover(NULL) == ETH_ARGUMENT);
    assert(eth_mac_get_statistics(&s) == ETH_OK && s.state == ETH_MAC_RUNNING);
	fake_eth.MACMDIOAR |= ETH_MACMDIOAR_MB;
	fake_eth.MACMDIODR = 0x1234U;
	assert(eth_mac_service() == ETH_MDIO && irq_enabled);
	assert(eth_mac_get_statistics(&s) == ETH_OK && s.state == ETH_MAC_RUNNING &&
		s.mdio_busy_on_entry == 1U && s.phy_register_read_errors == 1U);
	assert(s.first_mdio_fault.stage == ETH_MDIO_BUSY_ON_ENTRY &&
		s.first_mdio_fault.phy_register == 1U && s.first_mdio_fault.macmdiodr == 0x1234U &&
		(s.first_mdio_fault.macmdioar & ETH_MACMDIOAR_MB) != 0U);
	assert(eth_mac_service() == ETH_OK);
	assert(fake_eth.DMACCR == ETH_DMACCR_DSL_128BIT);
	assert(fake_eth.DMACTDRLR == 3U && fake_eth.DMACRDRLR == 3U);
	assert(eth_mac_tx_submit(frame, sizeof(frame), &ticket) == ETH_OK);
	assert(eth_mac_tx_reclaim(&done) == ETH_EMPTY);
	g_eth_dma_memory.tx[0].w[3] = ETH_DESC_FD | ETH_DESC_LD;
	fake_eth.DMACSR = ETH_DMACSR_TI | ETH_DMACSR_RI | ETH_DMACSR_TBU | ETH_DMACSR_RBU;
	ETH_IRQHandler();
	assert(eth_mac_tx_reclaim(&done) == ETH_OK && done == ticket);
	assert(eth_mac_get_statistics(&s) == ETH_OK && s.irq_tx == 1U && s.irq_rx == 1U &&
		s.irq_tbu == 1U && s.irq_rbu == 1U);
	fake_eth.DMACSR = ETH_DMACSR_FBE;
	ETH_IRQHandler();
	assert(fake_eth.DMACIER == 0U && eth_mac_tx_submit(frame, 60U, &ticket) == ETH_IO);
	assert(
		eth_mac_get_statistics(&s) == ETH_OK && s.state == ETH_MAC_FAULT && s.irq_dma_error == 1U);
	assert(eth_mac_service() == ETH_IO && !irq_enabled);
	assert(eth_mac_stop() == ETH_OK && !irq_enabled);
	assert(eth_mac_start() == ETH_OK);
	assert(eth_mac_tx_submit(frame, 60U, &ticket) == ETH_OK);
	phy_regs[1] = 0U;
	assert(eth_mac_service() == ETH_LINK_DOWN);
	assert(eth_mac_get_statistics(&s) == ETH_OK && s.state == ETH_MAC_READY &&
		s.ring.tx_aborted == 1U);
	phy_regs[1] = 36U;
	fail_flush = 1U;
	assert(eth_mac_start() == ETH_TIMEOUT);
	fail_flush = 0U;
	assert(eth_mac_stop() == ETH_OK);
	phy_regs[0] = 0U;
	assert(eth_mac_get_link(&link) == ETH_OK && link.speed_mbps == 10U && !link.full_duplex);
	phy_regs[0] = 0x2100U;
	assert(eth_mac_get_link(&link) == ETH_OK && link.speed_mbps == 100U && link.full_duplex);
	phy_regs[0] = 0x1000U;
	phy_regs[31] = 0x1014U;
	assert(eth_mac_get_link(&link) == ETH_OK && link.speed_mbps == 10U && link.full_duplex);
	phy_regs[31] = 0x1008U;
	assert(eth_mac_get_link(&link) == ETH_OK && link.speed_mbps == 100U && !link.full_duplex);
	phy_regs[31] = 0x1000U;
	assert(eth_mac_get_link(&link) == ETH_PHY_STATUS);
	phy_regs[31] = 0x1008U;
	fail_reset = 1U;
	assert(eth_mac_start() == ETH_TIMEOUT);
	fail_reset = 0U;
	assert(eth_mac_stop() == ETH_OK);
	phy_regs[2] = 0xFFFFU;
	assert(eth_mac_start() == ETH_PHY_MISSING);
	phy_regs[2] = 7U;
	fail_mdio = 1U;
	assert(eth_mac_start() == ETH_MDIO);
	assert(eth_mac_get_statistics(&s) == ETH_OK && s.mdio_wait_timeouts == 1U &&
		s.mdio_errors == 2U && s.phy_status_errors == 1U &&
		s.first_mdio_fault.stage == ETH_MDIO_BUSY_ON_ENTRY);
	fail_mdio = 0U;
	assert(eth_mac_stop() == ETH_OK);
	assert(eth_mac_start() == ETH_OK);
	fake_eth.MACISR = ETH_MACISR_RXSTSIS;
	fake_eth.MACRXTXSR = 0x100U;
	fake_eth.DMACSR = 0U;
	ETH_IRQHandler();
	assert(
		eth_mac_get_statistics(&s) == ETH_OK && s.irq_mac_error == 1U && s.state == ETH_MAC_FAULT);
	assert(eth_mac_stop() == ETH_OK);
	puts("PASS: MAC init/link/mode/start/stop, MDIO/reset/flush faults, DMA IRQ and recovery");
	return 0;
}
