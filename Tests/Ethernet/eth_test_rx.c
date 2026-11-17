//测试层逐帧校验，不改变驱动所有权或中断处理
#include "eth_test_rx.h"
#include <string.h>
volatile EthTestRxStatistics_t g_eth_test_rx_check;
static uint8_t s_seen[(ETH_TEST_RX_LIMIT + 7U) / 8U];
static const uint8_t s_destination[6] = {2U, 0x47U, 0x47U, 0U, 0U, 1U};
static const uint8_t s_source[6] = {0xECU, 0x1AU, 0xC3U, 0x3EU, 0xDBU, 0xB2U};
//新一轮固定从序号 1 开始，背景帧单独计数
void EthTestRx_Reset(void)
{
	EthTestRxStatistics_t empty = {0};

	g_eth_test_rx_check = empty;
	memset(s_seen, 0, sizeof(s_seen));
	g_eth_test_rx_check.missing = ETH_TEST_RX_LIMIT;
	g_eth_test_rx_check.active = 1U;
}
//仅在任务已复制完有效帧后检查完整内容
void EthTestRx_Inspect(
	const uint8_t *data,
	size_t length
)
{
	uint32_t sequence;
	uint32_t index;
	uint8_t mask;
	size_t i;

	if (!g_eth_test_rx_check.active)
		return;
	if (!data || length < 14U || memcmp(data, s_destination, 6U) != 0 ||
		memcmp(data + 6, s_source, 6U) != 0 || data[12] != 0x88U || data[13] != 0xB5U)
	{
		++g_eth_test_rx_check.ignored;
		return;
	}
	++g_eth_test_rx_check.received;
	if (length != 60U)
	{
		++g_eth_test_rx_check.bad;
		return;
	}
	sequence = ((uint32_t)data[14] << 24) | ((uint32_t)data[15] << 16) | ((uint32_t)data[16] << 8) |
		data[17];
	if (sequence == 0U || sequence > ETH_TEST_RX_LIMIT)
	{
		++g_eth_test_rx_check.bad;
		return;
	}
	for (i = 18U;
		 i < length;
		 ++i)
	{
		if (data[i] != (uint8_t)(i - 18U))
		{
			++g_eth_test_rx_check.bad;
			return;
		}
	}
	index = sequence - 1U;
	mask = (uint8_t)(1U << (index % 8U));
	if ((s_seen[index / 8U] & mask) != 0U)
	{
		++g_eth_test_rx_check.duplicates;
		return;
	}
	s_seen[index / 8U] |= mask;
	if (sequence < g_eth_test_rx_check.last_sequence)
		++g_eth_test_rx_check.out_of_order;
	else
		g_eth_test_rx_check.last_sequence = sequence;
	++g_eth_test_rx_check.unique;
	g_eth_test_rx_check.missing = ETH_TEST_RX_LIMIT - g_eth_test_rx_check.unique;
}
//显式结束采样，只有全部正确且无重复或乱序才能通过
void EthTestRx_Finish(void)
{
	g_eth_test_rx_check.active = 0U;
	g_eth_test_rx_check.passed =
		(g_eth_test_rx_check.unique == ETH_TEST_RX_LIMIT && g_eth_test_rx_check.bad == 0U &&
			g_eth_test_rx_check.duplicates == 0U && g_eth_test_rx_check.out_of_order == 0U);
}
