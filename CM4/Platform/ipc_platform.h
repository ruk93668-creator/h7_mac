#ifndef CM4_IPC_PLATFORM_H
#define CM4_IPC_PLATFORM_H

#include "ipc_core.h"

//执行编译器与CPU内存顺序屏障
void IPC_Platform_MemoryBarrier(void);

//返回本核静态协议端口，不向共享区发布函数指针
const IpcCorePort_t *IPC_Platform_GetPort(void);

#endif
