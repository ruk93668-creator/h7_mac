#ifndef IPC_CONFIG_H
#define IPC_CONFIG_H

//SRAM4共享窗口起点，两核Scatter必须保持相同
#define IPC_SHARED_BASE            (0x38000000UL)

//与两核Scatter及CM7 MPU Region1一致，结束地址不包含自身
#define IPC_SHARED_WINDOW_BYTES    (0x00001000UL)

//运行期心跳处理周期，不依赖OLED刷新
#define IPC_HEARTBEAT_PERIOD_MS      (100U)

//连续无进展的允许周期数，用于计算超时阈值
#define IPC_HEARTBEAT_MISS_LIMIT     (5U)

//本核观察对端心跳无进展的超时期限
#define IPC_HEARTBEAT_TIMEOUT_MS \
    (IPC_HEARTBEAT_PERIOD_MS * IPC_HEARTBEAT_MISS_LIMIT)

#endif
