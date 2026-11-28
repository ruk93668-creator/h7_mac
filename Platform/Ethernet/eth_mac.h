//STM32H747 原始以太网帧收发后端
#ifndef ETH_MAC_H
#define ETH_MAC_H

#include "eth_types.h"
#include <stddef.h>
typedef enum
{
	ETH_MAC_OFF = 0,
	ETH_MAC_READY,
	ETH_MAC_RUNNING,
	ETH_MAC_FAULT
} EthMacState_t;
typedef struct
{
	//当前物理链路状态
	uint8_t up;
	//自动协商完成标志
	uint8_t autoneg_complete;
	//全双工标志
	uint8_t full_duplex;
	//协商或强制模式速率
	uint16_t speed_mbps;
} EthLinkState_t;

typedef enum
{
	ETH_MDIO_NONE = 0,
	ETH_MDIO_BUSY_ON_ENTRY,
	ETH_MDIO_WAIT_TIMEOUT
} EthMdioStage_t;

typedef struct
{
	uint32_t stage;//EthMdioStage_t，NONE 表示尚未记录
	uint32_t tick_ms;//发生故障时的 HAL 时基，暖复位后属于前一轮启动
	uint32_t macmdioar;//失败现场，保留 MB、命令和时钟范围字段
	uint32_t macmdiodr;//失败现场，访问失败时不可视为有效 PHY 数据
	uint32_t hclk_hz;//失败时读取的 HCLK
	uint32_t mdc_clock_range;//配置的 MACMDIOAR.CR 编码
	uint32_t phy_address;
	uint32_t phy_register;
	uint32_t write;//0 读，1 写
} EthMdioFault_t;

typedef struct
{
	//MAC/DMA 软件运行状态
	EthMacState_t state;
	//收发环统计，不等同 IRQ 事件数
	EthRingStatistics_t ring;
	//发送完成中断事件数
	uint32_t irq_tx;
	//接收完成中断事件数
	uint32_t irq_rx;
	//发送描述符不足事件数
	uint32_t irq_tbu;
	//接收缓冲区不足事件数
	uint32_t irq_rbu;
	//DMA 总线、描述符或停止异常事件数
	uint32_t irq_dma_error;
	//MAC 收发状态异常事件数
	uint32_t irq_mac_error;
	//最近一次 DMACSR 状态快照
	uint32_t last_dma_status;
	//最近一次 MACRXTXSR 状态快照
	uint32_t last_mac_status;
	//初始化失败次数
	uint32_t init_errors;
	//启动失败次数
	uint32_t start_errors;
	//MDIO 访问失败次数
	uint32_t mdio_errors;
	//实际读取的 PHY 标识
	uint32_t phy_id;
	uint32_t mdio_busy_on_entry;
	uint32_t mdio_wait_timeouts;
	uint32_t phy_register_read_errors;//MDIO 读失败，包含在 mdio_errors 中
	uint32_t phy_status_errors;//寄存器读取成功，但协商结果不合法
	EthMdioFault_t first_mdio_fault;//驱动复位前锁存，CM7 正式构建使用 UNINIT
} EthStatistics_t;

//仅由同一任务串行调用，HAL 毫秒时基必须正常运行
//显式初始化，不注册 Application；板级须提供 50 MHz 和专用 MPU/链接配置
EthResult_t eth_mac_init(const uint8_t mac[6]);

//同一串行调用者显式恢复初始化失败；先停止 DMA，失败保留 FAULT。
//不得在 ISR 或仍有上层协议资源时调用；mac 生命周期与 init 相同。
EthResult_t eth_mac_recover(const uint8_t mac[6]);

EthResult_t eth_mac_start(void);

//有界复位并确认 DMA 停止后才能归还内部缓冲区
//未完成 ticket 计为中止，不计成功发送
EthResult_t eth_mac_stop(void);

EthResult_t eth_mac_get_link(EthLinkState_t *link_state);

EthResult_t eth_mac_get_address(uint8_t mac[6]);

EthResult_t eth_mac_get_statistics(EthStatistics_t *snapshot);

EthResult_t eth_mac_tx_submit(
	const void *data,
	size_t length,
	uint32_t *ticket
);

EthResult_t eth_mac_tx_reclaim(uint32_t *ticket);

EthResult_t eth_mac_rx_poll(
	void *data,
	size_t capacity,
	size_t *length
);

//同一任务定期检查链路，掉线或模式变化时停止 DMA
//故障后显式 stop/start，不自动重发
EthResult_t eth_mac_service(void);

//ISR 仅记录硬件事件，不处理描述符或调用 FreeRTOS
void ETH_IRQHandler(void);

//保留已有类型名，避免破坏现有 PHY 和收发调用接口
typedef EthMacState_t eth_mac_state;
typedef EthLinkState_t eth_link_state;
typedef EthStatistics_t eth_statistics;

#endif
