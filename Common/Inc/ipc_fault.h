#ifndef IPC_FAULT_H
#define IPC_FAULT_H

#include <stdint.h>

/* Accumulated IPC faults, independent of SysResult_t operation results.
 * Bit positions preserve the existing shared-memory protocol.
 */
typedef uint32_t IpcFaultFlags_t;

#define IPC_FAULT_MAGIC_TIMEOUT       (UINT32_C(1) << 0)
#define IPC_FAULT_VERSION_MISMATCH    (UINT32_C(1) << 1)
#define IPC_FAULT_PEER_NOT_READY      (UINT32_C(1) << 2)
#define IPC_FAULT_HEARTBEAT_TIMEOUT   (UINT32_C(1) << 3)
#define IPC_FAULT_HSEM                (UINT32_C(1) << 4)
#define IPC_FAULT_SHARED_INVALID      (UINT32_C(1) << 5)

#endif /* IPC_FAULT_H */
