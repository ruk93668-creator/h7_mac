# 正式架构审核

```text
CM7/
  ApplicationV3/
    Core/                     初始化、配置、消息与任务身份
    Runtime/                  Registry 配合固定服务清单、Mailbox、EventLoop、Queue Pool
    Tasks/                    ControlTask、Supervisor 与 IPC 状态发送
    Services/{Camera,USB,Storage}/
  Media/                      Raw 七缓冲、JPEG FIFO 与物理租约
  Service/                    UVC Class、Payload、Reader Proof 与 AVI
  Platform/                   USB Device、StorageFS、SD Block 和 IPC 平台
  BSP/                        DCMI、SDMMC、SDRAM 等硬件操作
Tests/ApplicationV3/BoardProfiles/
```

CameraTask 独占 Media 的物理所有权。Dispatcher 保存租约身份与已投递事务，按源通道识别消费者。USB 按 Reader Proof 确认旧提交退休后归还，Storage 同步写入返回后归还。Camera STOP 等待受理租约，已发布帧继续按原通道闭合。

ControlTask 是现有生命周期协调入口。生产调用者在 ControlTask 上下文使用 Supervisor Set、Query、Read、Recover，ControlTask 按原周期处理结果。Supervisor 只发业务命令。正式 BOOT 只准备资源并 发布 READY，不自动启动硬件，也不打开可写卷。现有 CM7/CM4 IPC 用于状态发布。后续 UI 控制需在批准的 ControlTask 接入点调用现有契约。

未来 Ethernet 可以用已批准的 Media 消费者枚举与逐消费者单租约模型。接入仍需确认消息路由、队列背压以及网络 DMA 的读者退出证据。OLED 从已发布 IPC 快照异步读取。QSPI 接入前核对 MPU、地址窗口、DMA 访问能力及链接区域。三个支线的接口与物理约束分别审核。

正式 Map 核对唯一 `App_Init`、`ControlTask_Create`、`AppV3_CreateThread` 和 `IpcTask_Create`。已归档 `map-audit.json`。
