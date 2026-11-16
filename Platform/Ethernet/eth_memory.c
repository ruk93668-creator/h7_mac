#include "eth_memory.h"
#include "stm32h7xx_hal.h"
#if defined(__CC_ARM)
eth_dma_memory g_eth_dma_memory
    __attribute__((section(".ethernet_dma"), zero_init, aligned(32)));
#else
eth_dma_memory g_eth_dma_memory
    __attribute__((section(".ethernet_dma"), aligned(32)));
#endif
typedef char memory_fits[(sizeof(eth_dma_memory)<=ETH_DMA_WINDOW)?1:-1];
void eth_memory_configure(void)
{
    MPU_Region_InitTypeDef r={0};
    r.Enable=MPU_REGION_ENABLE; r.Number=MPU_REGION_NUMBER4;
    r.BaseAddress=ETH_DMA_BASE; r.Size=MPU_REGION_SIZE_32KB;
    r.AccessPermission=MPU_REGION_FULL_ACCESS;
    r.DisableExec=MPU_INSTRUCTION_ACCESS_DISABLE;
    r.TypeExtField=MPU_TEX_LEVEL1; r.IsShareable=MPU_ACCESS_SHAREABLE;
    r.IsCacheable=MPU_ACCESS_NOT_CACHEABLE;
    r.IsBufferable=MPU_ACCESS_NOT_BUFFERABLE;
    HAL_MPU_ConfigRegion(&r);
}
int eth_memory_valid(void)
{
    uint32_t previous, mask, a, b, i;
    int valid=1;
    const uint32_t expected=MPU_RASR_ENABLE_Msk|MPU_RASR_XN_Msk|
        (3UL<<MPU_RASR_AP_Pos)|(1UL<<MPU_RASR_TEX_Pos)|MPU_RASR_S_Msk|
        (MPU_REGION_SIZE_32KB<<MPU_RASR_SIZE_Pos);
    if ((uintptr_t)&g_eth_dma_memory!=ETH_DMA_BASE ||
        (MPU->CTRL&MPU_CTRL_ENABLE_Msk)==0U) return 0;
    mask=__get_PRIMASK(); __disable_irq(); previous=MPU->RNR;
    MPU->RNR=4U; a=MPU->RBAR&MPU_RBAR_ADDR_Msk; b=MPU->RASR;
    if (a!=ETH_DMA_BASE || b!=expected) valid=0;
    /* Reject higher-priority overlays of any part of our reserved window. */
    for (i=5U;i<((MPU->TYPE>>8)&255U);++i) {
        uint32_t size, base;
        MPU->RNR=i; a=MPU->RBAR&MPU_RBAR_ADDR_Msk; b=MPU->RASR;
        if ((b&MPU_RASR_ENABLE_Msk)==0U) continue;
        size=((b&MPU_RASR_SIZE_Msk)>>MPU_RASR_SIZE_Pos)+1U;
        if (size>=32U) { valid=0; continue; }
        size=1UL<<size; base=a&~(size-1U);
        if (base<ETH_DMA_BASE+ETH_DMA_WINDOW && base+size>ETH_DMA_BASE) valid=0;
    }
    MPU->RNR=previous; __set_PRIMASK(mask); return valid;
}
