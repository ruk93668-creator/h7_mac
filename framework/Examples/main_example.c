#include "framework_init.h"
#include "framework_runtime.h"

//先完成目标板时钟与中断初始化
int main(void)
{
    SysResult_t result;

    result = Framework_InitRuntime();
    if ( result != SYS_OK)
    {
        //由目标板决定如何处理初始化失败
        for (;;)
        {
        }
    }

    for (;;)
    {
        Framework_Run();
    }
}
