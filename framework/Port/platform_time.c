#include "platform_time.h"

//由目标板实现毫秒时钟，读取时保证一致性
extern uint32_t Board_TimeGetMs(void);

//隔离目标板时钟实现
uint32_t Platform_TimeGetMs(void)
{
    return Board_TimeGetMs();
}
