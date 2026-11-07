#ifndef IPC_FAULT_H
#define IPC_FAULT_H

#include <stdint.h>

//累计IPC故障位，与单次操作结果分开；保持原有二进制位位置
typedef uint32_t IpcFaultFlags_t;

//等待有效协议标识超时，保留原故障位
#define IPC_FAULT_MAGIC_TIMEOUT       (UINT32_C(1) << 0)

//共享协议版本与本核固件不一致
#define IPC_FAULT_VERSION_MISMATCH    (UINT32_C(1) << 1)

//对端尚未完成准备，保留原故障位
#define IPC_FAULT_PEER_NOT_READY      (UINT32_C(1) << 2)

//本核观察到对端心跳长时间没有进展
#define IPC_FAULT_HEARTBEAT_TIMEOUT   (UINT32_C(1) << 3)

//HSEM操作异常，保留启动相关故障位
#define IPC_FAULT_HSEM                (UINT32_C(1) << 4)

//共享头、状态块或业务字段不符合协议
#define IPC_FAULT_SHARED_INVALID      (UINT32_C(1) << 5)

#endif /* IPC_FAULT_H */
