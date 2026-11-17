//验证测试统计器不会把缺帧、重复、乱序或损坏误判为通过
#include "eth_test_rx.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t s_frame[60] = {2U,	 0x47U, 0x47U, 0U,	  0U,	 1U,	0xECU,
							  0x1AU, 0xC3U, 0x3EU, 0xDBU, 0xB2U, 0x88U, 0xB5U};
static void Submit(uint32_t sequence)
{
	size_t i;
	s_frame[14] = (uint8_t)(sequence >> 24);
	s_frame[15] = (uint8_t)(sequence >> 16);
	s_frame[16] = (uint8_t)(sequence >> 8);
	s_frame[17] = (uint8_t)sequence;
	for (i = 18U; i < sizeof(s_frame); ++i)
		s_frame[i] = (uint8_t)(i - 18U);
	EthTestRx_Inspect(s_frame, sizeof(s_frame));
}
int main(void)
{
	uint32_t i;
	EthTestRx_Reset();
	for (i = 1U; i <= ETH_TEST_RX_LIMIT; ++i)
		Submit(i);
	EthTestRx_Finish();
	assert(g_eth_test_rx_check.passed && g_eth_test_rx_check.received == 1000U);
	assert(g_eth_test_rx_check.unique == 1000U && g_eth_test_rx_check.missing == 0U);
	EthTestRx_Reset();
	Submit(1U);
	Submit(1U);
	Submit(3U);
	Submit(2U);
	assert(g_eth_test_rx_check.duplicates == 1U && g_eth_test_rx_check.out_of_order == 1U);
	assert(g_eth_test_rx_check.unique == 3U && g_eth_test_rx_check.missing == 997U);
	s_frame[59] ^= 1U;
	EthTestRx_Inspect(s_frame, sizeof(s_frame));
	EthTestRx_Inspect(s_frame, 59U);
	Submit(0U);
	Submit(1001U);
	assert(g_eth_test_rx_check.bad == 4U);
	s_frame[12] = 8U;
	EthTestRx_Inspect(s_frame, sizeof(s_frame));
	assert(g_eth_test_rx_check.ignored == 1U);
	EthTestRx_Finish();
	assert(!g_eth_test_rx_check.passed);
	s_frame[12] = 0x88U;
	EthTestRx_Reset();
	for (i = 1U; i < ETH_TEST_RX_LIMIT; ++i)
		Submit(i);
	EthTestRx_Finish();
	assert(!g_eth_test_rx_check.passed && g_eth_test_rx_check.missing == 1U);
	Submit(1000U);
	assert(g_eth_test_rx_check.missing == 1U);
	puts("PASS: fixed RX payload, sequence, missing, duplicate, reorder and session gates");
	return 0;
}
