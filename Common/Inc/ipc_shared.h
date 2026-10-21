#ifndef IPC_SHARED_H
#define IPC_SHARED_H

#include <stdint.h>

#define IPC_MAGIC   (0x49504331UL)
#define IPC_VERSION (1UL)

typedef enum
{
    IPC_CORE_NOT_READY = 0U,
    IPC_CORE_READY = 1U
} IpcCoreReady_t;

typedef struct
{

	//魔数
    uint32_t magic;

	//IPC协议版本号
    uint32_t version;

	//是否准备好
    uint32_t m7_ready, m4_ready;

    //心跳，判断是否卡死
	uint32_t m7_heartbeat, m4_heartbeat;

	//错误状态
	uint32_t m7_err, m4_err;
} IpcShared_t;

//双核共享的IPC协议对象，由Service维护
extern volatile IpcShared_t g_ipc;

#endif /* IPC_SHARED_H */
