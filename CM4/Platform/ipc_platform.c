#include "ipc_platform.h"
#include "cmsis_compiler.h"

//保存并屏蔽本核中断，只保护本核重入
static uint32_t IpcPlatform_Enter(void);

//恢复本核进入前的中断屏蔽状态
static void IpcPlatform_Leave(uint32_t state);

//检查当前是否为普通执行上下文
static bool IpcPlatform_IsThread(void);

//静态端口驻留本核普通内存，不属于共享协议
static const IpcCorePort_t s_port =
{
    IPC_Platform_MemoryBarrier,
    IpcPlatform_Enter,
    IpcPlatform_Leave,
    IpcPlatform_IsThread
};

//保证共享访问的编译器与CPU顺序
void IPC_Platform_MemoryBarrier(void)
{

    //当前CMSIS ARMCC的DMB已包含两侧schedule barrier
    __COMPILER_BARRIER();
    __DMB();
    __COMPILER_BARRIER();
}

//返回本核静态端口
const IpcCorePort_t *IPC_Platform_GetPort(void)
{
    return &s_port;
}

//保存并屏蔽本核中断
static uint32_t IpcPlatform_Enter(void)
{
    uint32_t state = __get_PRIMASK();

    //屏蔽本核中断，防止本地状态复制或授权检查被打断
    __disable_irq();
    __COMPILER_BARRIER();
    return state;
}

//恢复本核中断屏蔽状态
static void IpcPlatform_Leave(uint32_t state)
{

    //先约束编译器重排，再恢复原屏蔽状态，不能无条件开中断
    __COMPILER_BARRIER();
    __set_PRIMASK(state);
}

//检查普通执行上下文
static bool IpcPlatform_IsThread(void)
{

    //IPSR为零表示线程模式，异常或ISR中拒绝任务侧操作
    return __get_IPSR() == 0U;
}
