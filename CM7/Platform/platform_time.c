#include "platform_time.h"
#include "stm32h7xx_hal.h"

uint32_t Platform_TimeGetMs(void)
{
    return HAL_GetTick();
}

