//固定内容测试帧校验，只用于实板验收固件
#ifndef ETH_TEST_RX_H
#define ETH_TEST_RX_H

#include <stddef.h>
#include <stdint.h>
#define ETH_TEST_RX_LIMIT 1000U
typedef struct
{
	uint32_t received;
	uint32_t unique;
	uint32_t bad;
	uint32_t duplicates;
	uint32_t out_of_order;
	uint32_t missing;
	uint32_t last_sequence;
	uint32_t ignored;
	uint32_t active;
	uint32_t passed;
} EthTestRxStatistics_t;
extern volatile EthTestRxStatistics_t g_eth_test_rx_check;
void EthTestRx_Reset(void);
void EthTestRx_Inspect(
	const uint8_t *data,
	size_t length
);
void EthTestRx_Finish(void);
#endif
