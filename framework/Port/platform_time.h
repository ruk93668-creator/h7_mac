#ifndef FRAMEWORK_PLATFORM_TIME_H
#define FRAMEWORK_PLATFORM_TIME_H

#include <stdint.h>

//返回持续递增的毫秒计数，允许32位自然回绕
uint32_t Platform_TimeGetMs(void);

#endif
