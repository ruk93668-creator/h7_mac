#include "main.h"
#include "arch/cc.h"

//正式CM7统一使用已有的致命错误入口
void eth_lwip_assert(const char *message, const char *file, unsigned line)
{
    (void)message;
    (void)file;
    (void)line;
    Error_Handler();
}
