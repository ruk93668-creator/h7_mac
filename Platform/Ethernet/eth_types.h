//Ethernet 任务接口和描述符环共用的结果与统计类型
#ifndef ETH_TYPES_H
#define ETH_TYPES_H
#include <stdint.h>

typedef enum
{
	ETH_OK = 0,
	ETH_EMPTY = 1,
	ETH_BUSY = 2,
	ETH_LINK_DOWN = 3,
	ETH_ARGUMENT = -1,
	ETH_IO = -2,
	ETH_STATE = -3,
	ETH_FRAME_ERROR = -4,
	ETH_CAPACITY = -5,
	ETH_TIMEOUT = -6,
	ETH_MEMORY = -7,
	ETH_PHY_MISSING = -8
} eth_result;
typedef struct
{
	uint32_t tx_submitted, tx_completed, tx_errors, tx_full, tx_aborted;
	uint32_t rx_delivered, rx_errors, rx_small, ownership_errors;
} eth_ring_statistics;

#endif
