//Ethernet 专用 MPU 和链接窗口
#include "eth_memory.h"
#include "stm32h7xx_hal.h"
#if defined(__CC_ARM)
EthDmaMemory_t g_eth_dma_memory __attribute__((section(".ethernet_dma"), zero_init, aligned(32)));
#else
EthDmaMemory_t g_eth_dma_memory __attribute__((section(".ethernet_dma"), aligned(32)));
#endif
typedef char memory_fits[(sizeof(EthDmaMemory_t) <= ETH_DMA_WINDOW) ? 1 : -1];

//对外接口实现

//配置早期启动的专用非缓存 MPU 区域
void eth_memory_configure(void)
{
	MPU_Region_InitTypeDef region = {0};

	region.Enable = MPU_REGION_ENABLE;
	region.Number = MPU_REGION_NUMBER4;
	region.BaseAddress = ETH_DMA_BASE;
	region.Size = MPU_REGION_SIZE_32KB;
	region.AccessPermission = MPU_REGION_FULL_ACCESS;
	region.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
	region.TypeExtField = MPU_TEX_LEVEL1;
	region.IsShareable = MPU_ACCESS_SHAREABLE;
	region.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
	region.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
	HAL_MPU_ConfigRegion(&region);
#ifdef ETH_NETWORK_SERVICE
    //仅 CPU 管理 lwIP 内存；DMA 描述符和 DMA Buffer 不落入该窗口。
    region.Number = MPU_REGION_NUMBER5;
    region.BaseAddress = 0x24010000U;
    region.Size = MPU_REGION_SIZE_64KB;
    region.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
    region.IsCacheable = MPU_ACCESS_CACHEABLE;
    region.IsBufferable = MPU_ACCESS_BUFFERABLE;
    HAL_MPU_ConfigRegion(&region);
#endif
}

//核对实际链接地址和 MPU 属性
int eth_memory_valid(void)
{
	uint32_t previous_region;
	uint32_t primask;
	uint32_t region_base;
	uint32_t attributes;
	uint32_t region_index;
	int memory_valid = 1;
	const uint32_t expected = MPU_RASR_ENABLE_Msk | MPU_RASR_XN_Msk | (3UL << MPU_RASR_AP_Pos) |
		(1UL << MPU_RASR_TEX_Pos) | MPU_RASR_S_Msk | (MPU_REGION_SIZE_32KB << MPU_RASR_SIZE_Pos);

	if ((uintptr_t)&g_eth_dma_memory != ETH_DMA_BASE || (MPU->CTRL & MPU_CTRL_ENABLE_Msk) == 0U)
		return 0;
	primask = __get_PRIMASK();
	__disable_irq();
	previous_region = MPU->RNR;
	MPU->RNR = 4U;
	region_base = MPU->RBAR & MPU_RBAR_ADDR_Msk;
	attributes = MPU->RASR;
	if (region_base != ETH_DMA_BASE || attributes != expected)
		memory_valid = 0;
	//拒绝高优先级 MPU 区域覆盖专用 DMA 窗口
	for (region_index = 5U;
		 region_index < ((MPU->TYPE >> 8) & 255U);
		 ++region_index)
	{
		uint32_t region_size;
		uint32_t region_start;
		MPU->RNR = region_index;
		region_base = MPU->RBAR & MPU_RBAR_ADDR_Msk;
		attributes = MPU->RASR;
		if ((attributes & MPU_RASR_ENABLE_Msk) == 0U)
			continue;
		region_size = ((attributes & MPU_RASR_SIZE_Msk) >> MPU_RASR_SIZE_Pos) + 1U;
		if (region_size >= 32U)
		{
			memory_valid = 0;
			continue;
		}
		region_size = 1UL << region_size;
		region_start = region_base & ~(region_size - 1U);
		if (region_start < ETH_DMA_BASE + ETH_DMA_WINDOW &&
			region_start + region_size > ETH_DMA_BASE)
			memory_valid = 0;
	}
	MPU->RNR = previous_region;
	__set_PRIMASK(primask);
	return memory_valid;
}
