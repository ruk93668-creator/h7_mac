//STM32H747 原始以太网帧收发后端
#include "eth_mac.h"
#include "eth_memory.h"
#ifdef ETH_MAC_HOST_TEST
#include "fake_hal.h"
#else
#include "stm32h7xx_hal.h"
#endif
#include <string.h>
#define DMA_IRQ_MASK                                                                               \
	(ETH_DMACIER_NIE | ETH_DMACIER_AIE | ETH_DMACIER_TIE | ETH_DMACIER_RIE | ETH_DMACIER_FBEE |    \
	 ETH_DMACIER_CDEE | ETH_DMACIER_RBUE | ETH_DMACIER_TBUE | ETH_DMACIER_TXSE | ETH_DMACIER_RSE)
#define DMA_W1C                                                                                    \
	(ETH_DMACSR_NIS | ETH_DMACSR_AIS | ETH_DMACSR_TI | ETH_DMACSR_RI | ETH_DMACSR_FBE |            \
	 ETH_DMACSR_CDE | ETH_DMACSR_RBU | ETH_DMACSR_TBU | ETH_DMACSR_TPS | ETH_DMACSR_RPS)
static eth_ring ring;
static eth_statistics statistics;
static eth_mac_state state = ETH_MAC_OFF;
static eth_link_state active_link;
static uint8_t mac_address[6];
static uint32_t mdio_div;
static volatile uint32_t irq_fault;
static volatile uint32_t irq_tx, irq_rx, irq_tbu, irq_rbu, irq_dma_error, irq_mac_error;
static volatile uint32_t last_dma, last_mac;
//按毫秒和轮询上限等待硬件清位
static int wait_clear(volatile uint32_t *reg, uint32_t bits, uint32_t timeout)
{
	uint32_t start = HAL_GetTick(), spins = 0U;
	while ((*reg & bits) != 0U)
	{
		if ((uint32_t)(HAL_GetTick() - start) >= timeout || ++spins >= 2000000U)
			return -1;
	}
	return 0;
}
//串行执行一次有界 MDIO 读写
static int mdio(void *context, uint8_t address, uint8_t reg, uint16_t *value, int write)
{
	(void)context;
	if ((ETH->MACMDIOAR & ETH_MACMDIOAR_MB) != 0U)
	{
		++statistics.mdio_errors;
		return -1;
	}
	if (write)
		ETH->MACMDIODR = *value;
	ETH->MACMDIOAR = mdio_div | ((uint32_t)address << 21) | ((uint32_t)reg << 16) |
					 (write ? ETH_MACMDIOAR_MOC_WR : ETH_MACMDIOAR_MOC_RD) | ETH_MACMDIOAR_MB;
	if (wait_clear(&ETH->MACMDIOAR, ETH_MACMDIOAR_MB, 10U) != 0)
	{
		++statistics.mdio_errors;
		return -1;
	}
	if (!write)
		*value = (uint16_t)ETH->MACMDIODR;
	return 0;
}
//适配 PHY 寄存器读取回调
static int phy_read(void *c, uint8_t a, uint8_t r, uint16_t *v)
{
	return mdio(c, a, r, v, 0);
}
//适配 PHY 寄存器写入回调
static int phy_write(void *c, uint8_t a, uint8_t r, uint16_t v)
{
	return mdio(c, a, r, &v, 1);
}
static const eth_phy phy = {NULL, phy_read, phy_write, 0U};
//保证数据、OWN 和尾指针的发布顺序
static void barrier(void *c)
{
	(void)c;
	__DSB();
}
//更新发送尾指针并触发 DMA 轮询
static void tx_kick(void *c, uint32_t tail)
{
	(void)c;
	ETH->DMACTDTPR = tail;
}
//更新接收尾指针并恢复 DMA 接收
static void rx_kick(void *c, uint32_t tail)
{
	(void)c;
	ETH->DMACRDTPR = tail;
}
static const eth_dma_ops ops = {NULL, barrier, tx_kick, rx_kick};
//按原理图配置 RMII 引脚并复位 PHY
static void pins(void)
{
	GPIO_InitTypeDef g = {0};
	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOC_CLK_ENABLE();
	__HAL_RCC_GPIOG_CLK_ENABLE();
	//PG9 控制 PHY 复位，RMII 输入和 strap 稳定前保持低电平
	HAL_GPIO_WritePin(GPIOG, GPIO_PIN_9, GPIO_PIN_RESET);
	g.Pin = GPIO_PIN_9;
	g.Mode = GPIO_MODE_OUTPUT_PP;
	g.Pull = GPIO_NOPULL;
	g.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOG, &g);
	g.Mode = GPIO_MODE_AF_PP;
	g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
	g.Alternate = GPIO_AF11_ETH;
	g.Pin = GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_7;
	HAL_GPIO_Init(GPIOA, &g);
	g.Pin = GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5;
	HAL_GPIO_Init(GPIOC, &g);
	g.Pin = GPIO_PIN_11 | GPIO_PIN_13 | GPIO_PIN_14;
	HAL_GPIO_Init(GPIOG, &g);
	HAL_Delay(2U);
	HAL_GPIO_WritePin(GPIOG, GPIO_PIN_9, GPIO_PIN_SET);
	HAL_Delay(10U);
}
//屏蔽中断并确认 MAC 和 DMA 已复位
static eth_result reset(void)
{
	HAL_NVIC_DisableIRQ(ETH_IRQn);
	ETH->DMACIER = 0U;
	ETH->MACIER = 0U;
	__HAL_RCC_ETH1MAC_FORCE_RESET();
	__DSB();
	(void)RCC->AHB1RSTR;
	__HAL_RCC_ETH1MAC_RELEASE_RESET();
	__DSB();
	ETH->DMAMR = ETH_DMAMR_SWR;
	if (wait_clear(&ETH->DMAMR, ETH_DMAMR_SWR, 100U) != 0)
		return ETH_TIMEOUT;
	HAL_NVIC_ClearPendingIRQ(ETH_IRQn);
	irq_fault = 0U;
	return ETH_OK;
}
//配置标准帧模式、本站地址和 DMA 参数
static void configure(void)
{
	//沿用标准帧间隔、前导码、CRC 检查和填充
	//不启用巨帧、TSO、校验和卸载、时间戳或上下文描述符
	ETH->MACMDIOAR = mdio_div;
	ETH->MACCR = ETH_MACCR_CST | ETH_MACCR_ACS; //类型帧与长度帧均去除 FCS
	ETH->MACPFR = 0U; //仅接收本站单播和广播，不启用混杂模式
	ETH->MACA0HR = ((uint32_t)mac_address[5] << 8) | mac_address[4];
	ETH->MACA0LR = ((uint32_t)mac_address[3] << 24) | ((uint32_t)mac_address[2] << 16) |
				   ((uint32_t)mac_address[1] << 8) | mac_address[0];
	ETH->MTLTQOMR |= ETH_MTLTQOMR_TSF;
	ETH->MTLRQOMR |= ETH_MTLRQOMR_RSF;
	ETH->DMASBMR = ETH_DMASBMR_AAL | ETH_DMASBMR_FB;
	ETH->DMACCR = ETH_DMACCR_DSL_128BIT;
	ETH->DMACTCR = 8UL << ETH_DMACTCR_TPBL_Pos;
	ETH->DMACRCR = (8UL << ETH_DMACRCR_RPBL_Pos) | (ETH_BUFFER_SIZE << 1);
	ETH->MMCRIMR = 0xFFFFFFFFU;
	ETH->MMCTIMR = 0xFFFFFFFFU;
	ETH->MACIER = ETH_MACIER_RXSTSIE | ETH_MACIER_TXSTSIE;
}
//准备板级资源并识别目标 PHY
eth_result eth_mac_init(const uint8_t mac[6])
{
	uint32_t hclk, or_value = 0U;
	unsigned i;
	eth_result result;
	if (!mac || (mac[0] & 1U) != 0U)
		return ETH_ARGUMENT;
	for (i = 0U; i < 6U; ++i)
		or_value |= mac[i];
	if (or_value == 0U)
		return ETH_ARGUMENT;
	if (state != ETH_MAC_OFF)
		return ETH_STATE;
	if (!eth_memory_valid())
		return ETH_MEMORY;
	memcpy(mac_address, mac, 6U);
	hclk = HAL_RCC_GetHCLKFreq();
	if (hclk < 20000000U || hclk > 300000000U)
		return ETH_ARGUMENT;
	mdio_div = hclk < 35000000U	   ? ETH_MACMDIOAR_CR_DIV16
			   : hclk < 60000000U  ? ETH_MACMDIOAR_CR_DIV26
			   : hclk < 100000000U ? ETH_MACMDIOAR_CR_DIV42
			   : hclk < 150000000U ? ETH_MACMDIOAR_CR_DIV62
			   : hclk < 250000000U ? ETH_MACMDIOAR_CR_DIV102
								   : ETH_MACMDIOAR_CR_DIV124;
	__HAL_RCC_SYSCFG_CLK_ENABLE();
	HAL_SYSCFG_ETHInterfaceSelect(SYSCFG_ETH_RMII);
	(void)SYSCFG->PMCR;
	__HAL_RCC_ETH1MAC_CLK_ENABLE();
	__HAL_RCC_ETH1TX_CLK_ENABLE();
	__HAL_RCC_ETH1RX_CLK_ENABLE();
	pins();
	result = reset();
	if (result != ETH_OK)
	{
		++statistics.init_errors;
		state = ETH_MAC_FAULT;
		return result;
	}
	configure();
	{
		eth_phy_result found = eth_phy_probe(&phy, &statistics.phy_id);
		if (found != ETH_PHY_OK)
		{
			++statistics.init_errors;
			state = ETH_MAC_FAULT;
			return found == ETH_PHY_IO ? ETH_IO : ETH_PHY_MISSING;
		}
	}
	state = ETH_MAC_READY;
	return ETH_OK;
}
//独立读取 PHY 链路和协商模式
eth_result eth_mac_get_link(eth_link_state *out)
{
	eth_phy_link link;
	uint16_t control, special;
	if (!out)
		return ETH_ARGUMENT;
	memset(out, 0, sizeof(*out));
	if (state == ETH_MAC_OFF)
		return ETH_STATE;
	if (eth_phy_read_link(&phy, &link) != ETH_PHY_OK)
		return ETH_IO;
	out->up = link.up;
	out->autoneg_complete = link.autoneg_complete;
	if (!link.up)
		return ETH_OK;
	if (phy_read(NULL, 0U, 0U, &control) != 0)
		return ETH_IO;
	if ((control & 0x1000U) != 0U)
	{
		if (!link.autoneg_complete)
			return ETH_BUSY;
		if (phy_read(NULL, 0U, 31U, &special) != 0)
			return ETH_IO;
		if ((special & 0x1000U) == 0U)
			return ETH_BUSY;
		special = (uint16_t)((special >> 2) & 7U);
		if (special != 1U && special != 2U && special != 5U && special != 6U)
			return ETH_IO;
		out->speed_mbps = (special & 2U) != 0U ? 100U : 10U;
		out->full_duplex = (uint8_t)((special & 4U) != 0U);
	}
	else
	{
		out->speed_mbps = (control & 0x2000U) != 0U ? 100U : 10U;
		out->full_duplex = (uint8_t)((control & 0x100U) != 0U);
	}
	return ETH_OK;
}
//确认链路后重建环并启动 MAC 和 DMA
eth_result eth_mac_start(void)
{
	eth_result result;
	eth_link_state link;
	eth_ring_statistics old;
	uint32_t sequence;
	if (state != ETH_MAC_READY || irq_fault)
		return ETH_STATE;
	if (!eth_memory_valid())
		return ETH_MEMORY;
	{
		eth_phy_result found = eth_phy_probe(&phy, &statistics.phy_id);
		if (found != ETH_PHY_OK)
			return found == ETH_PHY_IO ? ETH_IO : ETH_PHY_MISSING;
	}
	result = eth_mac_get_link(&link);
	if (result != ETH_OK || !link.up)
		return result != ETH_OK ? result : ETH_LINK_DOWN;
	result = reset();
	if (result != ETH_OK)
	{
		++statistics.start_errors;
		state = ETH_MAC_FAULT;
		return result;
	}
	configure();
	old = ring.statistics;
	sequence = ring.sequence;
	result = eth_ring_reset(&ring, &g_eth_dma_memory, &ops);
	if (result != ETH_OK)
	{
		++statistics.start_errors;
		state = ETH_MAC_FAULT;
		return result;
	}
	ring.statistics = old;
	ring.sequence = sequence;
	ETH->DMACTDLAR = (uint32_t)(uintptr_t)g_eth_dma_memory.tx;
	ETH->DMACRDLAR = (uint32_t)(uintptr_t)g_eth_dma_memory.rx;
	ETH->DMACTDRLR = ETH_RING_SIZE - 1U;
	ETH->DMACRDRLR = ETH_RING_SIZE - 1U;
	ETH->DMACTDTPR = (uint32_t)(uintptr_t)g_eth_dma_memory.tx;
	ETH->DMACRDTPR = (uint32_t)(uintptr_t)&g_eth_dma_memory.rx[ETH_RING_CAPACITY];
	if (link.speed_mbps == 100U)
		ETH->MACCR |= ETH_MACCR_FES;
	if (link.full_duplex)
		ETH->MACCR |= ETH_MACCR_DM;
	ETH->MTLTQOMR |= ETH_MTLTQOMR_FTQ;
	if (wait_clear(&ETH->MTLTQOMR, ETH_MTLTQOMR_FTQ, 10U) != 0)
	{
		++statistics.start_errors;
		state = ETH_MAC_FAULT;
		return ETH_TIMEOUT;
	}
	ETH->DMACSR = DMA_W1C;
	__DSB();
	ETH->MACCR |= ETH_MACCR_TE | ETH_MACCR_RE;
	ETH->DMACTCR |= ETH_DMACTCR_ST;
	ETH->DMACRCR |= ETH_DMACRCR_SR;
	if ((ETH->MACCR & (ETH_MACCR_TE | ETH_MACCR_RE)) != (ETH_MACCR_TE | ETH_MACCR_RE) ||
		(ETH->DMACTCR & ETH_DMACTCR_ST) == 0U || (ETH->DMACRCR & ETH_DMACRCR_SR) == 0U)
	{
		++statistics.start_errors;
		state = ETH_MAC_FAULT;
		return ETH_IO;
	}
	active_link = link;
	state = ETH_MAC_RUNNING;
	ETH->DMACIER = DMA_IRQ_MASK;
	//保留现有 NVIC 优先级，ISR 不调用内核接口
	HAL_NVIC_ClearPendingIRQ(ETH_IRQn);
	HAL_NVIC_EnableIRQ(ETH_IRQn);
	return ETH_OK;
}
//确认硬件复位后记录中止的发送
eth_result eth_mac_stop(void)
{
	eth_result result;
	if (state == ETH_MAC_OFF)
		return ETH_STATE;
	result = reset();
	if (result != ETH_OK)
	{
		state = ETH_MAC_FAULT;
		return result;
	}
	ring.statistics.tx_aborted += ring.tx_count;
	ring.tx_count = 0U;
	configure();
	state = ETH_MAC_READY;
	return ETH_OK;
}
//检查运行状态、故障锁存和 MPU 属性
static eth_result running(void)
{
	if (irq_fault)
		return ETH_IO;
	if (state != ETH_MAC_RUNNING)
		return ETH_STATE;
	if (!eth_memory_valid())
		return ETH_MEMORY;
	return ETH_OK;
}
//检查运行门禁后提交内部发送副本
eth_result eth_mac_tx_submit(const void *data, size_t n, uint32_t *ticket)
{
	eth_result r = running();
	return r == ETH_OK ? eth_ring_tx(&ring, data, n, ticket) : r;
}
//检查运行门禁后回收发送完成
eth_result eth_mac_tx_reclaim(uint32_t *ticket)
{
	eth_result r = running();
	return r == ETH_OK ? eth_ring_reclaim(&ring, ticket) : r;
}
//检查运行门禁后复制接收帧
eth_result eth_mac_rx_poll(void *data, size_t n, size_t *length)
{
	eth_result r = running();
	return r == ETH_OK ? eth_ring_rx(&ring, data, n, length) : r;
}
//在任务中处理链路变化及中断锁存故障
eth_result eth_mac_service(void)
{
	eth_link_state link;
	eth_result r;
	if (irq_fault)
	{
		r = eth_mac_stop();
		state = ETH_MAC_FAULT;
		return r == ETH_OK ? ETH_IO : r;
	}
	r = running();
	if (r != ETH_OK)
		return r;
	r = eth_mac_get_link(&link);
	if (r != ETH_OK || !link.up || link.speed_mbps != active_link.speed_mbps ||
		link.full_duplex != active_link.full_duplex)
	{
		eth_result stopped = eth_mac_stop();
		return stopped != ETH_OK ? stopped : (r != ETH_OK ? r : ETH_LINK_DOWN);
	}
	return ETH_OK;
}
//返回配置的本站 MAC 地址
eth_result eth_mac_get_address(uint8_t mac[6])
{
	if (!mac)
		return ETH_ARGUMENT;
	if (state == ETH_MAC_OFF)
		return ETH_STATE;
	memcpy(mac, mac_address, 6U);
	return ETH_OK;
}
//短暂屏蔽中断以取得一致统计快照
eth_result eth_mac_get_statistics(eth_statistics *out)
{
	uint32_t mask;
	if (!out)
		return ETH_ARGUMENT;
	mask = __get_PRIMASK();
	__disable_irq();
	*out = statistics;
	out->ring = ring.statistics;
	out->state = irq_fault ? ETH_MAC_FAULT : state;
	out->irq_tx = irq_tx;
	out->irq_rx = irq_rx;
	out->irq_tbu = irq_tbu;
	out->irq_rbu = irq_rbu;
	out->irq_dma_error = irq_dma_error;
	out->irq_mac_error = irq_mac_error;
	out->last_dma_status = last_dma;
	out->last_mac_status = last_mac;
	__set_PRIMASK(mask);
	return ETH_OK;
}
//确认硬件事件并锁存故障，不处理缓冲区
void ETH_IRQHandler(void)
{
	uint32_t s = ETH->DMACSR, m = ETH->MACISR;
	last_dma = s;
	if (s & ETH_DMACSR_TI)
		++irq_tx;
	if (s & ETH_DMACSR_RI)
		++irq_rx;
	if (s & ETH_DMACSR_TBU)
		++irq_tbu;
	if (s & ETH_DMACSR_RBU)
		++irq_rbu;
	if (s & (ETH_DMACSR_FBE | ETH_DMACSR_CDE | ETH_DMACSR_TPS | ETH_DMACSR_RPS))
	{
		++irq_dma_error;
		irq_fault = 1U;
		ETH->DMACIER = 0U;
	}
	if (m & (ETH_MACISR_RXSTSIS | ETH_MACISR_TXSTSIS))
	{
		last_mac = ETH->MACRXTXSR;
		++irq_mac_error;
		irq_fault = 1U;
		ETH->MACIER = 0U;
		ETH->DMACIER = 0U;
	}
	ETH->DMACSR = s & DMA_W1C;
	__DSB();
}
