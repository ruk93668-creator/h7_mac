#ifndef CM7_PLATFORM_SYNC_H
#define CM7_PLATFORM_SYNC_H

#include <stdint.h>

//读取当前异常号，零表示普通任务上下文
uint32_t Platform_SyncGetException(void);

//读取当前全局中断屏蔽值
uint32_t Platform_SyncGetPrimask(void);

//读取当前中断优先级屏蔽值
uint32_t Platform_SyncGetBasepri(void);

//屏蔽可屏蔽中断，不修改BASEPRI
void Platform_SyncDisableIrq(void);

//恢复调用者保存的PRIMASK，支持嵌套屏蔽
void Platform_SyncRestoreIrq(uint32_t saved);

//保持原有数据内存屏障顺序
void Platform_SyncMemoryBarrier(void);

//读取外部中断优先级，参数为IRQ编号
uint32_t Platform_SyncGetIrqPriority(uint32_t irq);

//成对保存并恢复原值；不能用固定清零代替Restore，也不代表资源已经释放
#endif
