#include "platform_time.h"
#include "lwip/sys.h"

//lwIP定时器使用与CM7业务相同的毫秒时基
u32_t sys_now(void)
{
    return Platform_TimeGetMs();
}
