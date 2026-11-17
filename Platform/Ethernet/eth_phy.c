//LAN8742A MDIO 基础接口
#include "eth_phy.h"
#include <stddef.h>

//内部功能函数声明
static int EthPhy_Valid(const EthPhy_t *phy);

//对外接口实现

//读取并核对 PHY 器件标识
EthPhyResult_t eth_phy_probe(
	const EthPhy_t *phy,
	uint32_t *id
)
{
	uint16_t identifier_high;
	uint16_t identifier_low;

	if (!EthPhy_Valid(phy) || id == NULL)
		return ETH_PHY_ARGUMENT;
	*id = 0U;
	if (phy->read(phy->context, phy->address, 2U, &identifier_high) != 0 ||
		phy->read(phy->context, phy->address, 3U, &identifier_low) != 0)
		return ETH_PHY_IO;
	*id = ((uint32_t)identifier_high << 16) | identifier_low;
	//核对 LAN8742A OUI 和型号，允许不同芯片修订号
	return ((*id & 0xFFFFFFF0UL) == 0x0007C130UL) ? ETH_PHY_OK : ETH_PHY_ID;
}

//发起 PHY 软件复位
EthPhyResult_t eth_phy_reset_begin(const EthPhy_t *phy)
{
	if (!EthPhy_Valid(phy))
		return ETH_PHY_ARGUMENT;
	return phy->write(phy->context, phy->address, 0U, 0x8000U) == 0 ? ETH_PHY_OK : ETH_PHY_IO;
}

//非阻塞检查 PHY 复位完成状态
EthPhyResult_t eth_phy_reset_poll(const EthPhy_t *phy)
{
	uint16_t value;

	if (!EthPhy_Valid(phy))
		return ETH_PHY_ARGUMENT;
	if (phy->read(phy->context, phy->address, 0U, &value) != 0)
		return ETH_PHY_IO;
	return (value & 0x8000U) != 0U ? ETH_PHY_WAIT : ETH_PHY_OK;
}

//双读状态寄存器取得当前链路状态
EthPhyResult_t eth_phy_read_link(
	const EthPhy_t *phy,
	EthPhyLink_t *link
)
{
	uint16_t value;

	if (!EthPhy_Valid(phy) || link == NULL)
		return ETH_PHY_ARGUMENT;
	link->up = 0U;
	link->autoneg_complete = 0U;
	if (phy->read(phy->context, phy->address, 1U, &value) != 0 ||
		phy->read(phy->context, phy->address, 1U, &value) != 0)
		return ETH_PHY_IO;
	link->up = (uint8_t)((value & 4U) != 0U);
	link->autoneg_complete = (uint8_t)((value & 32U) != 0U);
	return ETH_PHY_OK;
}

//内部功能函数实现

//检查 MDIO 回调及 PHY 地址
static int EthPhy_Valid(const EthPhy_t *phy)
{
	return phy != NULL && phy->read != NULL && phy->write != NULL && phy->address < 32U;
}
