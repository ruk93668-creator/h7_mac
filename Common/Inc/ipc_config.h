#ifndef IPC_CONFIG_H
#define IPC_CONFIG_H

#define IPC_SHARED_BASE            (0x38000000UL)

//与两核Scatter及CM7 MPU Region1一致，结束地址不包含自身
#define IPC_SHARED_WINDOW_BYTES    (0x00001000UL)

#define IPC_HEARTBEAT_PERIOD_MS      (100U)
#define IPC_HEARTBEAT_MISS_LIMIT     (5U)

#define IPC_HEARTBEAT_TIMEOUT_MS \
    (IPC_HEARTBEAT_PERIOD_MS * IPC_HEARTBEAT_MISS_LIMIT)

#endif
