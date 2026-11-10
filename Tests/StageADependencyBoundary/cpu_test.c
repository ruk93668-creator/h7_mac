//链接真实CMSIS实现，验证ARM机器指令下的屏蔽值和嵌套恢复
#include "platform_sync.h"
int Cpu_Test(unsigned initial)
{
    unsigned outer,inner;
    if(Platform_SyncGetPrimask()!=initial)return __LINE__;
    outer=Platform_SyncGetPrimask();
    Platform_SyncDisableIrq();
    inner=Platform_SyncGetPrimask();
    Platform_SyncDisableIrq();
    Platform_SyncMemoryBarrier();
    Platform_SyncRestoreIrq(inner);
    if(Platform_SyncGetPrimask()!=1U)return __LINE__;
    Platform_SyncRestoreIrq(outer);
    if(Platform_SyncGetPrimask()!=initial)return __LINE__;
    if(Platform_SyncGetBasepri()!=0x50U)return __LINE__;
    if(Platform_SyncGetException()!=37U)return __LINE__;
    if(Platform_SyncGetIrqPriority(9U)!=7U)return __LINE__;
    return 0;
}
