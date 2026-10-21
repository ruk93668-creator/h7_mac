#include "ipc_service.h"
#include "ipc_platform.h"
#include "ipc_shared.h"
#include "ipc_fault.h"
#include "framework_init.h"

void IPC_Service_EarlyInit(void)
{
	/* App Ipc init */
	g_ipc = (IpcShared_t){0};
	g_ipc.version = IPC_VERSION;
    /*
     * 保证上面的初始化数据先发布，
     * magic 最后作为“共享区有效”标志。
     */	
	IPC_Platform_MemoryBarrier();
	
	g_ipc.magic = IPC_MAGIC;
	
	IPC_Platform_MemoryBarrier();
}

static SysResult_t IPC_Service_Init(void)
{
	IPC_Platform_MemoryBarrier();
	
    g_ipc.m7_ready = IPC_CORE_READY;
	
	return SYS_OK;
}
/*  INIT_REGISTER 
	M7_IPC_Service_Init
	LEVEL-SERVICE
*/
FRAMEWORK_INIT_REGISTER(
    IPC_Service_Init,
    INIT_LEVEL_SERVICE
);
