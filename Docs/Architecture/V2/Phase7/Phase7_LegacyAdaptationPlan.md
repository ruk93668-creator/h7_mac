# Phase 7 既有 IPC V3 与传输适配计划

## 源码确认

- `Common/Inc/ipc_shared.h` 的 IPC_VERSION 为 3。共享布局 320 B，M7/M4 各 128 B 的唯一写入状态块，保留字段不作为消息通道。`Common/Inc/ipc_config.h` 的 SRAM4 窗口为 4 KiB。`Common/Src/ipc_core.c` 提供运行态发布、ReadLocal/ReadPeer 和状态快照一致性检查。
- `CM7/Application/Tasks/ipc_task.c` 从原状态源发布应用状态，`CM7/Service/ipc_service.c::IPC_Service_SetApplication` 是应用状态发布入口。`CM4/Service/ipc_service.c::IPC_Service_GetApplicationStatus` 读取状态供显示使用。心跳、peer ready、HSEM 启动/通知和状态序号均不提供控制请求、授权、业务完成或结果消费事务。
- `CM7/Service/usb_mode.c::USB_Mode_Stop` 会调用 USB_Device_Stop。真实 USB 控制与 UVC 共享物理设备时，始终可恢复的控制入口待正式适配。Phase 7 使用逻辑控制端口，不申请媒体资源。
- 原 52B/56B、IPC V3 和 Lifecycle Replay 连同 M4 OLED 延时均保持。新本地 struct 没有冻结的跨核或网络 wire ABI。

## 正式接入门禁

| 入口 | 最小适配及验证 |
| --- | --- |
| M4 IPC | 经单独批准定义版本化控制请求/回复/确认、真实启动身份及权限；验证双核重启、旧消息失效、所有权、内存屏障。原状态窗口保持可用。 |
| USB | 通过现有 USB 层的独立控制接口收发，先审核停止 UVC 后恢复入口仍可达；覆盖重连、慢端和队列压力。 |
| BLE / Wi-Fi / Ethernet | 由各已认证连接生成可信 route，做长度/codec 初筛再发布值消息。认证、密钥、会话和无线/网络驱动不在独立模块中实例化。 |
| 旧 BOOT、Watch、ControlTask | 经 Phase 8 接入审批建立唯一控制入口，把旧操作映射成同一请求；产品明确 profile/权限配置和控制恢复入口。Keil 登记放在该集成工作内审批。 |
| Execution / 各任务 | 使用独立工作、结果和 ACK 队列。接收结果与实际消费独立，通知仅唤醒。真实 owner 提供一致的模块状态及已绑定 profile。 |
| 资源与实际硬件 | 在真机核验 USB/Storage 独立退出、真实 Lease/ACK、DMA 静止、掉卡及错误恢复。客户端断线不会归还 Camera 帧或替代 STOP。 |

本阶段无产品线上的适配器授权、无线/USB 驱动改动或配置切换。两项资源证明仍为 NOT_PROVEN。
