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
	ETH_PHY_MISSING = -8,
	ETH_MDIO = -9,//MDIO 未完成，当前 PHY 状态未知
	ETH_PHY_STATUS = -10//PHY 状态已读出，协商模式非法
} EthResult_t;
typedef struct
{
	//已接收的发送提交数
	uint32_t tx_submitted;
	//成功回收的发送数
	uint32_t tx_completed;
	//对应操作的失败次数
	uint32_t tx_errors;
	//内部发送槽耗尽次数
	uint32_t tx_full;
	//停止时中止的未回收发送
	uint32_t tx_aborted;
	//已复制给调用者的接收帧数
	uint32_t rx_delivered;
	//对应操作的失败次数
	uint32_t rx_errors;
	//调用者接收容量不足次数
	uint32_t rx_small;
	//对应操作的失败次数
	uint32_t ownership_errors;
} EthRingStatistics_t;

//保留已有类型名，避免破坏现有 PHY 和收发调用接口
typedef EthResult_t eth_result;
typedef EthRingStatistics_t eth_ring_statistics;

#endif
