#include "ipc_platform.h"
#include "ipc_shared.h"
#include "cmsis_compiler.h"

void IPC_Platform_MemoryBarrier(void)
{
    __DMB();
}
