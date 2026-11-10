#include "platform_sync.h"
#include "stm32h7xx.h"

//读取当前异常号，零表示普通任务上下文
uint32_t Platform_SyncGetException(void)
{

    //直接执行原CPU操作，不增加锁、等待或额外屏障
    return __get_IPSR();
}

//读取当前全局中断屏蔽值
uint32_t Platform_SyncGetPrimask(void)
{

    //直接执行原CPU操作，不增加锁、等待或额外屏障
    return __get_PRIMASK();
}

//读取当前中断优先级屏蔽值
uint32_t Platform_SyncGetBasepri(void)
{

    //直接执行原CPU操作，不增加锁、等待或额外屏障
    return __get_BASEPRI();
}

//屏蔽可屏蔽中断，不修改BASEPRI
void Platform_SyncDisableIrq(void)
{

    //直接执行原CPU操作，不增加锁、等待或额外屏障
    __disable_irq();
}

//恢复调用者保存的PRIMASK，支持嵌套屏蔽
void Platform_SyncRestoreIrq(uint32_t saved)
{

    //直接执行原CPU操作，不增加锁、等待或额外屏障
    __set_PRIMASK(saved);
}

//保持原有数据内存屏障顺序
void Platform_SyncMemoryBarrier(void)
{

    //直接执行原CPU操作，不增加锁、等待或额外屏障
    __DMB();
}

//读取外部中断优先级，参数为IRQ编号
uint32_t Platform_SyncGetIrqPriority(uint32_t irq)
{

    //直接执行原CPU操作，不增加锁、等待或额外屏障
    return NVIC_GetPriority((IRQn_Type)irq);
}
