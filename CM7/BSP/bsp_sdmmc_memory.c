//SDMMC缓冲区独立放入AXI，MPU只覆盖这一块32KiB区域
#include "bsp_sdmmc.h"
#include "stm32h7xx_hal.h"
#include "framework_init.h"

//链接器将本段独占放在0x24000000，不允许普通数据进入同一区域
uint8_t g_sdmmc_dma_buffer[BSP_SDMMC_DMA_BYTES]
    __attribute__((section(".sdmmc_dma"), zero_init, aligned(32)));

//编译时核对MPU区域与块大小，宏改变后不能静默产生属性覆盖
typedef char Sdmmc_DmaSizeCheck[(BSP_SDMMC_DMA_BYTES == (1UL << (MPU_REGION_SIZE_32KB + 1U))) ? 1 : -1];
typedef char Sdmmc_DmaBaseCheck[(BSP_SDMMC_DMA_BASE & (BSP_SDMMC_DMA_BYTES - 1U)) == 0U ? 1 : -1];
typedef char Sdmmc_DmaBlockCheck[(BSP_SDMMC_DMA_BYTES % BSP_SDMMC_BLOCK_SIZE) == 0U ? 1 : -1];

//保存启动期配置结果，失败只禁止SD操作，不影响Camera和USB初始化
static bool s_memory_ready;

//准备内存并保留独立故障状态，不让可选SD设备阻止Framework启动
static SysResult_t Sdmmc_MemoryPrepare(void);

//启动期配置SDMMC专用MPU区域
SysResult_t BSP_SDMMC_MemoryInit(void)
{
    MPU_Region_InitTypeDef region = {0};
    uint32_t primask;

    //Cache已经开启时不修改未知旧属性，避免遗留脏行覆盖DMA数据
    if (__get_IPSR() != 0U || (SCB->CCR & SCB_CCR_DC_Msk) != 0U ||
        (uint32_t)g_sdmmc_dma_buffer != BSP_SDMMC_DMA_BASE)
        return SYS_ERR_INVALID_STATE;

    //配置发生在任务运行前，只在MPU更新期间短暂屏蔽中断
    primask = __get_PRIMASK();
    __disable_irq();
    HAL_MPU_Disable();
    region.Enable = MPU_REGION_ENABLE;
    region.Number = MPU_REGION_NUMBER3;
    region.BaseAddress = BSP_SDMMC_DMA_BASE;
    region.Size = MPU_REGION_SIZE_32KB;
    region.AccessPermission = MPU_REGION_FULL_ACCESS;
    region.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
    region.TypeExtField = MPU_TEX_LEVEL1;
    region.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
    region.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
    region.IsShareable = MPU_ACCESS_SHAREABLE;
    HAL_MPU_ConfigRegion(&region);
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
    __DSB();
    __ISB();
    __set_PRIMASK(primask);
    s_memory_ready = true;
    return SYS_OK;
}

//核对链接位置和实际MPU属性
bool BSP_SDMMC_MemoryValid(void)
{
    uint32_t primask, previous, base, attributes;
    uint32_t expected = MPU_RASR_ENABLE_Msk | MPU_RASR_XN_Msk |
        (3UL << MPU_RASR_AP_Pos) | (1UL << MPU_RASR_TEX_Pos) |
        MPU_RASR_S_Msk | (MPU_REGION_SIZE_32KB << MPU_RASR_SIZE_Pos);

    //地址和容量必须由真实链接结果满足，不能用类型转换掩盖DTCM地址
    if (!s_memory_ready || (uint32_t)g_sdmmc_dma_buffer != BSP_SDMMC_DMA_BASE ||
        sizeof(g_sdmmc_dma_buffer) != BSP_SDMMC_DMA_BYTES || (MPU->CTRL & MPU_CTRL_ENABLE_Msk) == 0U)
        return false;

    //RNR是共享选择寄存器，读回后恢复原值，避免影响其他MPU检查
    primask = __get_PRIMASK();
    __disable_irq();
    previous = MPU->RNR;
    MPU->RNR = MPU_REGION_NUMBER3;
    base = MPU->RBAR & MPU_RBAR_ADDR_Msk;
    attributes = MPU->RASR;
    MPU->RNR = previous;
    __set_PRIMASK(primask);
    return base == BSP_SDMMC_DMA_BASE && attributes == expected;
}

//注册启动期内存准备
static SysResult_t Sdmmc_MemoryPrepare(void)
{
    (void)BSP_SDMMC_MemoryInit();
    return SYS_OK;
}

FRAMEWORK_INIT_REGISTER(Sdmmc_MemoryPrepare, INIT_LEVEL_PLATFORM);
