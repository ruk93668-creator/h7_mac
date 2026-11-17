//Ethernet 专用 MPU 和链接窗口
#ifndef ETH_MEMORY_H
#define ETH_MEMORY_H
#include "eth_dma.h"
#define ETH_DMA_BASE UINT32_C(0x24078000)
#define ETH_DMA_WINDOW UINT32_C(0x8000)
extern eth_dma_memory g_eth_dma_memory;
//仅在 CM7 早期启动、MPU 和 DCache 禁用时配置
void eth_memory_configure(void);
int eth_memory_valid(void);
#endif
