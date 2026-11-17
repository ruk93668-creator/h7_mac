//LAN8742A MDIO 基础接口
#ifndef ETH_PHY_H
#define ETH_PHY_H
#include <stdint.h>
//MDIO 回调成功返回 0，硬件等待必须有界
//所有操作由调用者串行执行，包括复位和链路轮询
typedef struct
{
	void *context;
	int (*read)(void *, uint8_t, uint8_t, uint16_t *);
	int (*write)(void *, uint8_t, uint8_t, uint16_t);
	uint8_t address;
} eth_phy;
typedef enum
{
	ETH_PHY_OK = 0,
	ETH_PHY_IO = -1,
	ETH_PHY_ARGUMENT = -2,
	ETH_PHY_ID = -3,
	ETH_PHY_WAIT = 1
} eth_phy_result;
typedef struct
{
	uint8_t up, autoneg_complete;
} eth_phy_link;
//只探测板级指定地址，不自动选择其他 PHY
eth_phy_result eth_phy_probe(const eth_phy *phy, uint32_t *id);
//仅执行软件复位，硬件复位时序及 strap 由板级负责
eth_phy_result eth_phy_reset_begin(const eth_phy *phy);
eth_phy_result eth_phy_reset_poll(const eth_phy *phy);
//双读 BSR 清除锁存低位，不推断协商速率或双工模式
eth_phy_result eth_phy_read_link(const eth_phy *phy, eth_phy_link *link);
#endif
