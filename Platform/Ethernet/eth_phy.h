//LAN8742A MDIO 基础接口
#ifndef ETH_PHY_H
#define ETH_PHY_H

#include <stdint.h>
//MDIO 回调成功返回 0，硬件等待必须有界
//所有操作由调用者串行执行，包括复位和链路轮询
typedef struct
{
	//回调上下文，由调用者管理
	void *context;
	int (*read)(void *context, uint8_t address, uint8_t reg, uint16_t *value);
	int (*write)(void *context, uint8_t address, uint8_t reg, uint16_t value);
	//板级 PHY 地址
	uint8_t address;
} EthPhy_t;
typedef enum
{
	ETH_PHY_OK = 0,
	ETH_PHY_IO = -1,
	ETH_PHY_ARGUMENT = -2,
	ETH_PHY_ID = -3,
	ETH_PHY_WAIT = 1
} EthPhyResult_t;
typedef struct
{
	//当前物理链路状态
	uint8_t up;
	//自动协商完成标志
	uint8_t autoneg_complete;
} EthPhyLink_t;

//只探测板级指定地址，不自动选择其他 PHY
EthPhyResult_t eth_phy_probe(
	const EthPhy_t *phy,
	uint32_t *id
);

//仅执行软件复位，硬件复位时序及 strap 由板级负责
EthPhyResult_t eth_phy_reset_begin(const EthPhy_t *phy);

EthPhyResult_t eth_phy_reset_poll(const EthPhy_t *phy);

//双读 BSR 清除锁存低位，不推断协商速率或双工模式
EthPhyResult_t eth_phy_read_link(
	const EthPhy_t *phy,
	EthPhyLink_t *link
);

//保留已有类型名，避免破坏现有 PHY 和收发调用接口
typedef EthPhy_t eth_phy;
typedef EthPhyResult_t eth_phy_result;
typedef EthPhyLink_t eth_phy_link;

#endif
