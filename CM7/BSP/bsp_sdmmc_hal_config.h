#ifndef CM7_BSP_SDMMC_HAL_CONFIG_H
#define CM7_BSP_SDMMC_HAL_CONFIG_H

//CM7统一预包含，避免CubeMX覆盖SD模块选择
#ifndef HAL_SD_MODULE_ENABLED
#define HAL_SD_MODULE_ENABLED
#endif

//限制HAL状态读取及等待传输态的单次软件超时，单位毫秒
#ifndef SDMMC_SWDATATIMEOUT
#define SDMMC_SWDATATIMEOUT 1000U
#endif

#endif
