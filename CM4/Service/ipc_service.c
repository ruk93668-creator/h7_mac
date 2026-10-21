#include "ipc_service.h"
#include "ipc_platform.h"
#include "ipc_shared.h"
#include "ipc_fault.h"
#include "framework_init.h"

//内部功能函数声明
//检查共享协议并标记CM4就绪
static SysResult_t IPC_Service_Init(void);

FRAMEWORK_INIT_REGISTER(
    IPC_Service_Init,
    INIT_LEVEL_SERVICE
);

//内部功能函数实现
static SysResult_t IPC_Service_Init(void)
{
	if (g_ipc.magic != IPC_MAGIC)
	{
		g_ipc.m4_err |= IPC_FAULT_SHARED_INVALID;
		return SYS_ERR_PROTOCOL;
	}
	
	IPC_Platform_MemoryBarrier();
	
	if ( g_ipc.version != IPC_VERSION)
	{
		g_ipc.m4_err |= IPC_FAULT_VERSION_MISMATCH;
		return SYS_ERR_PROTOCOL;
	}
	
	IPC_Platform_MemoryBarrier();
	
	g_ipc.m4_ready = IPC_CORE_READY;

	return SYS_OK;
}
