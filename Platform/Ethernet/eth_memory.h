#ifndef ETH_MEMORY_H
#define ETH_MEMORY_H
#include "eth_dma.h"
#define ETH_DMA_BASE UINT32_C(0x24078000)
#define ETH_DMA_WINDOW UINT32_C(0x8000)
extern eth_dma_memory g_eth_dma_memory;
/* Only call while MPU and DCache are disabled at early CM7 boot. */
void eth_memory_configure(void);
int eth_memory_valid(void);
#endif
