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
} eth_mac_state;
typedef struct
{
	uint8_t up, autoneg_complete, full_duplex;
	uint16_t speed_mbps;
} eth_link_state;
typedef struct
{
	eth_mac_state state;
	eth_ring_statistics ring;
	uint32_t irq_tx, irq_rx, irq_tbu, irq_rbu, irq_dma_error, irq_mac_error;
	uint32_t last_dma_status, last_mac_status, init_errors, start_errors, mdio_errors;
	uint32_t phy_id;
} eth_statistics;
//仅由同一任务串行调用，HAL 毫秒时基必须正常运行
//显式初始化，不注册 Application；板级须提供 50 MHz 和专用 MPU/链接配置
eth_result eth_mac_init(const uint8_t mac[6]);
eth_result eth_mac_start(void);
//有界复位并确认 DMA 停止后才能归还内部缓冲区
//未完成 ticket 计为中止，不计成功发送
eth_result eth_mac_stop(void);
eth_result eth_mac_get_link(eth_link_state *);
eth_result eth_mac_get_address(uint8_t mac[6]);
eth_result eth_mac_get_statistics(eth_statistics *);
eth_result eth_mac_tx_submit(const void *, size_t, uint32_t *ticket);
eth_result eth_mac_tx_reclaim(uint32_t *ticket);
eth_result eth_mac_rx_poll(void *, size_t, size_t *length);
//同一任务定期检查链路，掉线或模式变化时停止 DMA
//故障后显式 stop/start，不自动重发
eth_result eth_mac_service(void);
//ISR 仅记录硬件事件，不处理描述符或调用 FreeRTOS
void ETH_IRQHandler(void);
#endif
