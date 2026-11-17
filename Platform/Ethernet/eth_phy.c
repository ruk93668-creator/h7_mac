//LAN8742A MDIO 基础接口
#include "eth_phy.h"
#include <stddef.h>
//检查 MDIO 回调及 PHY 地址
static int valid(const eth_phy *p)
{
	return p != NULL && p->read != NULL && p->write != NULL && p->address < 32U;
}
//读取并核对 PHY 器件标识
eth_phy_result eth_phy_probe(const eth_phy *p, uint32_t *id)
{
	uint16_t a, b;
	if (!valid(p) || id == NULL)
		return ETH_PHY_ARGUMENT;
	*id = 0U;
	if (p->read(p->context, p->address, 2U, &a) != 0 ||
		p->read(p->context, p->address, 3U, &b) != 0)
		return ETH_PHY_IO;
	*id = ((uint32_t)a << 16) | b;
	//核对 LAN8742A OUI 和型号，允许不同芯片修订号
	return ((*id & 0xFFFFFFF0UL) == 0x0007C130UL) ? ETH_PHY_OK : ETH_PHY_ID;
}
//发起 PHY 软件复位
eth_phy_result eth_phy_reset_begin(const eth_phy *p)
{
	if (!valid(p))
		return ETH_PHY_ARGUMENT;
	return p->write(p->context, p->address, 0U, 0x8000U) == 0 ? ETH_PHY_OK : ETH_PHY_IO;
}
//非阻塞检查 PHY 复位完成状态
eth_phy_result eth_phy_reset_poll(const eth_phy *p)
{
	uint16_t value;
	if (!valid(p))
		return ETH_PHY_ARGUMENT;
	if (p->read(p->context, p->address, 0U, &value) != 0)
		return ETH_PHY_IO;
	return (value & 0x8000U) != 0U ? ETH_PHY_WAIT : ETH_PHY_OK;
}
//双读状态寄存器取得当前链路状态
eth_phy_result eth_phy_read_link(const eth_phy *p, eth_phy_link *link)
{
	uint16_t value;
	if (!valid(p) || link == NULL)
		return ETH_PHY_ARGUMENT;
	link->up = 0U;
	link->autoneg_complete = 0U;
	if (p->read(p->context, p->address, 1U, &value) != 0 ||
		p->read(p->context, p->address, 1U, &value) != 0)
		return ETH_PHY_IO;
	link->up = (uint8_t)((value & 4U) != 0U);
	link->autoneg_complete = (uint8_t)((value & 32U) != 0U);
	return ETH_PHY_OK;
}
