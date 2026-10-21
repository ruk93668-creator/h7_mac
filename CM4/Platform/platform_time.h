#ifndef CM4_PLATFORM_TIME_H
#define CM4_PLATFORM_TIME_H

/* CM4 platform time interface. */
#include <stdint.h>

//获取当前核心的毫秒Tick，计数允许回绕
uint32_t Platform_TimeGetMs(void);

#endif /* CM4_PLATFORM_TIME_H */
