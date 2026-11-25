//STM32H747 原始以太网帧收发后端
#include "eth_mac.h"
#include "eth_phy.h"
#include "eth_board.h"
#include "eth_memory.h"
#ifdef ETH_MAC_HOST_TEST
#include "fake_hal.h"
#else
#include "stm32h7xx_hal.h"
#endif
#include <string.h>
#define ETH_MAC_MDIO_TIMEOUT_MS 10U
#define ETH_MAC_RESET_TIMEOUT_MS 100U
#define ETH_MAC_FLUSH_TIMEOUT_MS 10U
#define ETH_MAC_WAIT_SPIN_LIMIT 2000000U

#define ETH_MAC_DMA_IRQ_MASK \
	(ETH_DMACIER_NIE | ETH_DMACIER_AIE | ETH_DMACIER_TIE | ETH_DMACIER_RIE | ETH_DMACIER_FBEE | \
		ETH_DMACIER_CDEE | ETH_DMACIER_RBUE | ETH_DMACIER_TBUE | ETH_DMACIER_TXSE | \
		ETH_DMACIER_RSE)
#define ETH_MAC_DMA_W1C \
	(ETH_DMACSR_NIS | ETH_DMACSR_AIS | ETH_DMACSR_TI | ETH_DMACSR_RI | ETH_DMACSR_FBE | \
		ETH_DMACSR_CDE | ETH_DMACSR_RBU | ETH_DMACSR_TBU | ETH_DMACSR_TPS | ETH_DMACSR_RPS)
static EthDmaRing_t s_ring;
static EthStatistics_t s_statistics;
static EthMacState_t s_state = ETH_MAC_OFF;
static EthLinkState_t s_active_link;
static uint8_t s_mac_address[6];
static uint32_t s_mdio_div;
static volatile uint32_t s_irq_fault;
static volatile uint32_t s_irq_tx;
static volatile uint32_t s_irq_rx;
static volatile uint32_t s_irq_tbu;
static volatile uint32_t s_irq_rbu;
static volatile uint32_t s_irq_dma_error;
static volatile uint32_t s_irq_mac_error;
static volatile uint32_t s_last_dma;
static volatile uint32_t s_last_mac;

//内部功能函数声明
static int EthMac_WaitClear(
	volatile uint32_t *reg,
	uint32_t bits,
	uint32_t timeout
);

static int EthMac_AccessPhyRegister(
	void *context,
	uint8_t address,
	uint8_t reg,
	uint16_t *value,
	int write
);

static int EthMac_ReadPhyRegister(
	void *context,
	uint8_t phy_address,
	uint8_t reg,
	uint16_t *value
);

static int EthMac_WritePhyRegister(
	void *context,
	uint8_t phy_address,
	uint8_t reg,
	uint16_t value
);

static void EthMac_MemoryBarrier(void *context);

static void EthMac_KickTx(
	void *context,
	uint32_t tail
);

static void EthMac_KickRx(
	void *context,
	uint32_t tail
);

static EthResult_t EthMac_Reset(void);

static void EthMac_Configure(void);

static EthResult_t EthMac_CheckRunning(void);

//当前板卡固定地址，回调只承接 MDIO 访问
static const EthPhy_t s_phy =
{
	.context = NULL,
	.read = EthMac_ReadPhyRegister,
	.write = EthMac_WritePhyRegister,
	.address = 0U
};

//描述符环只通过这组接口执行屏障和更新尾指针
static const EthDmaOps_t s_ops =
{
	.context = NULL,
	.barrier = EthMac_MemoryBarrier,
	.tx_kick = EthMac_KickTx,
	.rx_kick = EthMac_KickRx
};

//对外接口实现

//准备板级资源并识别目标 PHY
EthResult_t eth_mac_init(const uint8_t mac[6])
{
	uint32_t hclk_hz;
	uint32_t address_bits = 0U;
	unsigned index;
	EthResult_t result;

	if (mac == NULL || (mac[0] & 1U) != 0U)
		return ETH_ARGUMENT;
	for (index = 0U;
		 index < 6U;
		 ++index)
		address_bits |= mac[index];
	if (address_bits == 0U)
		return ETH_ARGUMENT;
	if (s_state != ETH_MAC_OFF)
		return ETH_STATE;
	if (!eth_memory_valid())
		return ETH_MEMORY;
	memcpy(s_mac_address, mac, 6U);
	hclk_hz = HAL_RCC_GetHCLKFreq();
	if (hclk_hz < 20000000U || hclk_hz > 300000000U)
		return ETH_ARGUMENT;
	if (hclk_hz < 35000000U)
		s_mdio_div = ETH_MACMDIOAR_CR_DIV16;
	else if (hclk_hz < 60000000U)
		s_mdio_div = ETH_MACMDIOAR_CR_DIV26;
	else if (hclk_hz < 100000000U)
		s_mdio_div = ETH_MACMDIOAR_CR_DIV42;
	else if (hclk_hz < 150000000U)
		s_mdio_div = ETH_MACMDIOAR_CR_DIV62;
	else if (hclk_hz < 250000000U)
		s_mdio_div = ETH_MACMDIOAR_CR_DIV102;
	else
		s_mdio_div = ETH_MACMDIOAR_CR_DIV124;

	EthBoard_Prepare();
	result = EthMac_Reset();
	if (result != ETH_OK)
	{
		++s_statistics.init_errors;
		s_state = ETH_MAC_FAULT;
		return result;
	}
	EthMac_Configure();
	{
		EthPhyResult_t found = eth_phy_probe(&s_phy, &s_statistics.phy_id);
		if (found != ETH_PHY_OK)
		{
			++s_statistics.init_errors;
			s_state = ETH_MAC_FAULT;
			return found == ETH_PHY_IO ? ETH_IO : ETH_PHY_MISSING;
		}
	}
	s_state = ETH_MAC_READY;
	return ETH_OK;
}

//仅显式恢复使用。先证明 DMA 停止，再重新执行板级和 PHY 识别。
EthResult_t eth_mac_recover(const uint8_t mac[6])
{
    EthResult_t result;
    unsigned index;
    uint8_t address_bits = 0U;
    if (mac == NULL || (mac[0] & 1U) != 0U)
        return ETH_ARGUMENT;
    for (index = 0U; index < 6U; ++index)
        address_bits |= mac[index];
    if (address_bits == 0U)
        return ETH_ARGUMENT;
    if (s_state != ETH_MAC_OFF)
    {
        result = eth_mac_stop();
        if (result != ETH_OK)
            return result;
    }
    s_state = ETH_MAC_OFF;
    return eth_mac_init(mac);
}

//独立读取 PHY 链路和协商模式
EthResult_t eth_mac_get_link(EthLinkState_t *snapshot)
{
	EthPhyLink_t link;
	uint16_t control;
	uint16_t special;

	if (snapshot == NULL)
		return ETH_ARGUMENT;
	memset(snapshot, 0, sizeof(*snapshot));
	if (s_state == ETH_MAC_OFF)
		return ETH_STATE;
	if (eth_phy_read_link(&s_phy, &link) != ETH_PHY_OK)
		return ETH_IO;
	snapshot->up = link.up;
	snapshot->autoneg_complete = link.autoneg_complete;
	if (!link.up)
		return ETH_OK;
	if (EthMac_ReadPhyRegister(NULL, 0U, 0U, &control) != 0)
		return ETH_IO;
	if ((control & 0x1000U) != 0U)
	{
		if (!link.autoneg_complete)
			return ETH_BUSY;
		if (EthMac_ReadPhyRegister(NULL, 0U, 31U, &special) != 0)
			return ETH_IO;
		if ((special & 0x1000U) == 0U)
			return ETH_BUSY;
		special = (uint16_t)((special >> 2) & 7U);
		if (special != 1U && special != 2U && special != 5U && special != 6U)
			return ETH_IO;
		snapshot->speed_mbps = (special & 2U) != 0U ? 100U : 10U;
		snapshot->full_duplex = (uint8_t)((special & 4U) != 0U);
	}
	else
	{
		snapshot->speed_mbps = (control & 0x2000U) != 0U ? 100U : 10U;
		snapshot->full_duplex = (uint8_t)((control & 0x100U) != 0U);
	}
	return ETH_OK;
}

//确认链路后重建环并启动 MAC 和 DMA
EthResult_t eth_mac_start(void)
{
	EthResult_t result;
	EthLinkState_t link;
	EthRingStatistics_t old;
	uint32_t sequence;

	if (s_state != ETH_MAC_READY || s_irq_fault)
		return ETH_STATE;
	if (!eth_memory_valid())
		return ETH_MEMORY;
	{
		EthPhyResult_t found = eth_phy_probe(&s_phy, &s_statistics.phy_id);
		if (found != ETH_PHY_OK)
			return found == ETH_PHY_IO ? ETH_IO : ETH_PHY_MISSING;
	}
	result = eth_mac_get_link(&link);
	if (result != ETH_OK || !link.up)
		return result != ETH_OK ? result : ETH_LINK_DOWN;
	result = EthMac_Reset();
	if (result != ETH_OK)
	{
		++s_statistics.start_errors;
		s_state = ETH_MAC_FAULT;
		return result;
	}
	EthMac_Configure();
	old = s_ring.statistics;
	sequence = s_ring.sequence;
	result = eth_ring_reset(&s_ring, &g_eth_dma_memory, &s_ops);
	if (result != ETH_OK)
	{
		++s_statistics.start_errors;
		s_state = ETH_MAC_FAULT;
		return result;
	}
	s_ring.statistics = old;
	s_ring.sequence = sequence;
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
	if (EthMac_WaitClear(&ETH->MTLTQOMR, ETH_MTLTQOMR_FTQ, ETH_MAC_FLUSH_TIMEOUT_MS) != 0)
	{
		++s_statistics.start_errors;
		s_state = ETH_MAC_FAULT;
		return ETH_TIMEOUT;
	}
	ETH->DMACSR = ETH_MAC_DMA_W1C;
	__DSB();
	ETH->MACCR |= ETH_MACCR_TE | ETH_MACCR_RE;
	ETH->DMACTCR |= ETH_DMACTCR_ST;
	ETH->DMACRCR |= ETH_DMACRCR_SR;
	if ((ETH->MACCR & (ETH_MACCR_TE | ETH_MACCR_RE)) != (ETH_MACCR_TE | ETH_MACCR_RE) ||
		(ETH->DMACTCR & ETH_DMACTCR_ST) == 0U || (ETH->DMACRCR & ETH_DMACRCR_SR) == 0U)
	{
		++s_statistics.start_errors;
		s_state = ETH_MAC_FAULT;
		return ETH_IO;
	}
	s_active_link = link;
	s_state = ETH_MAC_RUNNING;
	ETH->DMACIER = ETH_MAC_DMA_IRQ_MASK;
	//保留现有 NVIC 优先级，ISR 不调用内核接口
	HAL_NVIC_ClearPendingIRQ(ETH_IRQn);
	HAL_NVIC_EnableIRQ(ETH_IRQn);
	return ETH_OK;
}

//确认硬件复位后记录中止的发送
EthResult_t eth_mac_stop(void)
{
	EthResult_t result;

	if (s_state == ETH_MAC_OFF)
		return ETH_STATE;
	result = EthMac_Reset();
	if (result != ETH_OK)
	{
		s_state = ETH_MAC_FAULT;
		return result;
	}
	s_ring.statistics.tx_aborted += s_ring.tx_count;
	s_ring.tx_count = 0U;
	EthMac_Configure();
	s_state = ETH_MAC_READY;
	return ETH_OK;
}

//检查运行门禁后提交内部发送副本
EthResult_t eth_mac_tx_submit(
	const void *data,
	size_t length,
	uint32_t *ticket
)
{
	EthResult_t result = EthMac_CheckRunning();

	return result == ETH_OK ? eth_ring_tx(&s_ring, data, length, ticket) : result;
}

//检查运行门禁后回收发送完成
EthResult_t eth_mac_tx_reclaim(uint32_t *ticket)
{
	EthResult_t result = EthMac_CheckRunning();

	return result == ETH_OK ? eth_ring_reclaim(&s_ring, ticket) : result;
}

//检查运行门禁后复制接收帧
EthResult_t eth_mac_rx_poll(
	void *data,
	size_t capacity,
	size_t *length
)
{
	EthResult_t result = EthMac_CheckRunning();

	return result == ETH_OK ? eth_ring_rx(&s_ring, data, capacity, length) : result;
}

//在任务中处理链路变化及中断锁存故障
EthResult_t eth_mac_service(void)
{
	EthLinkState_t link;
	EthResult_t result;

	if (s_irq_fault)
	{
		result = eth_mac_stop();
		s_state = ETH_MAC_FAULT;
		return result == ETH_OK ? ETH_IO : result;
	}
	result = EthMac_CheckRunning();
	if (result != ETH_OK)
		return result;
	result = eth_mac_get_link(&link);
	if (result != ETH_OK || !link.up || link.speed_mbps != s_active_link.speed_mbps ||
		link.full_duplex != s_active_link.full_duplex)
	{
		EthResult_t stopped = eth_mac_stop();
		return stopped != ETH_OK ? stopped : (result != ETH_OK ? result : ETH_LINK_DOWN);
	}
	return ETH_OK;
}

//返回配置的本站 MAC 地址
EthResult_t eth_mac_get_address(uint8_t mac[6])
{
	if (mac == NULL)
		return ETH_ARGUMENT;
	if (s_state == ETH_MAC_OFF)
		return ETH_STATE;
	memcpy(mac, s_mac_address, 6U);
	return ETH_OK;
}

//短暂屏蔽中断以取得一致统计快照
EthResult_t eth_mac_get_statistics(EthStatistics_t *snapshot)
{
	uint32_t mask;

	if (snapshot == NULL)
		return ETH_ARGUMENT;
	mask = __get_PRIMASK();
	__disable_irq();
	*snapshot = s_statistics;
	snapshot->ring = s_ring.statistics;
	snapshot->state = s_irq_fault ? ETH_MAC_FAULT : s_state;
	snapshot->irq_tx = s_irq_tx;
	snapshot->irq_rx = s_irq_rx;
	snapshot->irq_tbu = s_irq_tbu;
	snapshot->irq_rbu = s_irq_rbu;
	snapshot->irq_dma_error = s_irq_dma_error;
	snapshot->irq_mac_error = s_irq_mac_error;
	snapshot->last_dma_status = s_last_dma;
	snapshot->last_mac_status = s_last_mac;
	__set_PRIMASK(mask);
	return ETH_OK;
}

//确认硬件事件并锁存故障，不处理缓冲区
void ETH_IRQHandler(void)
{
	uint32_t dma_status = ETH->DMACSR;
	uint32_t mac_status = ETH->MACISR;

	s_last_dma = dma_status;
	if (dma_status & ETH_DMACSR_TI)
		++s_irq_tx;
	if (dma_status & ETH_DMACSR_RI)
		++s_irq_rx;
	if (dma_status & ETH_DMACSR_TBU)
		++s_irq_tbu;
	if (dma_status & ETH_DMACSR_RBU)
		++s_irq_rbu;
	if (dma_status & (ETH_DMACSR_FBE | ETH_DMACSR_CDE | ETH_DMACSR_TPS | ETH_DMACSR_RPS))
	{
		++s_irq_dma_error;
		s_irq_fault = 1U;
		ETH->DMACIER = 0U;
	}
	if (mac_status & (ETH_MACISR_RXSTSIS | ETH_MACISR_TXSTSIS))
	{
		s_last_mac = ETH->MACRXTXSR;
		++s_irq_mac_error;
		s_irq_fault = 1U;
		ETH->MACIER = 0U;
		ETH->DMACIER = 0U;
	}
	ETH->DMACSR = dma_status & ETH_MAC_DMA_W1C;
	__DSB();
}

//内部功能函数实现

//按毫秒和轮询上限等待硬件清位
static int EthMac_WaitClear(
	volatile uint32_t *reg,
	uint32_t bits,
	uint32_t timeout
)
{
	uint32_t start = HAL_GetTick();
	uint32_t spins = 0U;

	while ((*reg & bits) != 0U)
	{
		if ((uint32_t)(HAL_GetTick() - start) >= timeout || ++spins >= ETH_MAC_WAIT_SPIN_LIMIT)
			return -1;
	}
	return 0;
}

//串行执行一次有界 MDIO 读写
static int EthMac_AccessPhyRegister(
	void *context,
	uint8_t address,
	uint8_t reg,
	uint16_t *value,
	int write
)
{
	(void)context;
	if ((ETH->MACMDIOAR & ETH_MACMDIOAR_MB) != 0U)
	{
		++s_statistics.mdio_errors;
		return -1;
	}
	if (write)
		ETH->MACMDIODR = *value;
	ETH->MACMDIOAR = s_mdio_div | ((uint32_t)address << 21) | ((uint32_t)reg << 16) |
		(write ? ETH_MACMDIOAR_MOC_WR : ETH_MACMDIOAR_MOC_RD) | ETH_MACMDIOAR_MB;
	if (EthMac_WaitClear(&ETH->MACMDIOAR, ETH_MACMDIOAR_MB, ETH_MAC_MDIO_TIMEOUT_MS) != 0)
	{
		++s_statistics.mdio_errors;
		return -1;
	}
	if (!write)
		*value = (uint16_t)ETH->MACMDIODR;
	return 0;
}

//适配 PHY 寄存器读取回调
static int EthMac_ReadPhyRegister(
	void *context,
	uint8_t phy_address,
	uint8_t reg,
	uint16_t *value
)
{
	return EthMac_AccessPhyRegister(context, phy_address, reg, value, 0);
}

//适配 PHY 寄存器写入回调
static int EthMac_WritePhyRegister(
	void *context,
	uint8_t phy_address,
	uint8_t reg,
	uint16_t value
)
{
	return EthMac_AccessPhyRegister(context, phy_address, reg, &value, 1);
}

//保证数据、OWN 和尾指针的发布顺序
static void EthMac_MemoryBarrier(void *context)
{
	(void)context;
	__DSB();
}

//更新发送尾指针并触发 DMA 轮询
static void EthMac_KickTx(
	void *context,
	uint32_t tail
)
{
	(void)context;
	ETH->DMACTDTPR = tail;
}

//更新接收尾指针并恢复 DMA 接收
static void EthMac_KickRx(
	void *context,
	uint32_t tail
)
{
	(void)context;
	ETH->DMACRDTPR = tail;
}

//屏蔽中断并确认 MAC 和 DMA 已复位
static EthResult_t EthMac_Reset(void)
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
	if (EthMac_WaitClear(&ETH->DMAMR, ETH_DMAMR_SWR, ETH_MAC_RESET_TIMEOUT_MS) != 0)
		return ETH_TIMEOUT;
	HAL_NVIC_ClearPendingIRQ(ETH_IRQn);
	s_irq_fault = 0U;
	return ETH_OK;
}

//配置标准帧模式、本站地址和 DMA 参数
static void EthMac_Configure(void)
{
	//沿用标准帧间隔、前导码、CRC 检查和填充
	//不启用巨帧、TSO、校验和卸载、时间戳或上下文描述符
	ETH->MACMDIOAR = s_mdio_div;
	ETH->MACCR = ETH_MACCR_CST | ETH_MACCR_ACS; //类型帧与长度帧均去除 FCS
	ETH->MACPFR = 0U; //仅接收本站单播和广播，不启用混杂模式
	ETH->MACA0HR = ((uint32_t)s_mac_address[5] << 8) | s_mac_address[4];
	ETH->MACA0LR = ((uint32_t)s_mac_address[3] << 24) | ((uint32_t)s_mac_address[2] << 16) |
		((uint32_t)s_mac_address[1] << 8) | s_mac_address[0];
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

//检查运行状态、故障锁存和 MPU 属性
static EthResult_t EthMac_CheckRunning(void)
{
	if (s_irq_fault)
		return ETH_IO;
	if (s_state != ETH_MAC_RUNNING)
		return ETH_STATE;
	if (!eth_memory_valid())
		return ETH_MEMORY;
	return ETH_OK;
}
