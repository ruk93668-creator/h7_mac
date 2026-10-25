#ifndef CM7_BSP_USB_HAL_CONFIG_H
#define CM7_BSP_USB_HAL_CONFIG_H

//通过CM7预包含启用PCD，避免CubeMX覆盖模块选择
#ifndef HAL_PCD_MODULE_ENABLED
#define HAL_PCD_MODULE_ENABLED
#endif
#endif
