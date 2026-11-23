# ApplicationV3 软件实现与集中验收

## 交付范围

本次从 `58be2de664776931335197e6c1aa4220826b5a5b` 接续，在 `codex/application-v3-media-usb` 完成 V3.2B、V3.3 和 V3.5。V3.2C Ethernet Network Service 与 V3.4 CM4 OLED / IPC 由用户独立负责，状态均为 **PENDING**。本次保留原 CM4、IPC、Keil 用户配置及未提交的 A4.1 本地工作。Wi-Fi 保留消费者身份。实板验收集中另行安排。

| 目录 | 职责 |
| --- | --- |
| `CM7/ApplicationV3/Core` | 启动配置和固定消息路由。Storage 两条 FIFO 使用原禁用命令通道的静态槽 |
| `CM7/ApplicationV3/Runtime` | 原 Record、Mailbox、EventLoop、Queue Pool、Business ACK/FAULT |
| `CM7/ApplicationV3/Services/Camera` | CameraTask 推进采集，Dispatcher 按消费者持有实际 Media Lease 及消息终结身份 |
| `CM7/ApplicationV3/Services/USB` | UsbTask 在 A3 Raw profile 下推进 UVC 和 Reader Proof |
| `CM7/ApplicationV3/Services/Storage` | StorageTask 管理挂载、录像、样本写入、文件收尾与帧归还 |
| `CM7/ApplicationV3/Tasks` | 原 ControlTask 协调服务依赖，原 IPC Task 保持 |
| `CM7/Media` 与 `CM7/Service`、`CM7/Platform` | 原 Raw 七缓冲及 JPEG FIFO；原 UVC Class/Payload、MJPEG AVI、StorageFS 和 SD 后端 |

## 数据面和边界

| 消费者 | 已有生产调用链 | 验收状态 |
| --- | --- | --- |
| USB | CameraCapture Raw Acquire → Dispatcher → UsbTask → UVC Submit/Process → 匹配 Reader Proof → RETURN/ACK/ACK_CONSUMED | A3 Raw profile 软件 PASS。Windows 出图以 A4 实板证据独立验收 |
| Storage | JPEG FIFO 原 SD Loan → Dispatcher → StorageTask → MjpegAvi_AppendFrame → StorageFS_Write → FatFS/SDMMC → 原身份 RETURN/ACK/ACK_CONSUMED | V3.2B 软件 PASS。实卡录像 NOT_RUN |
| Ethernet | 等待用户提供 Network Service 契约和实现 | PENDING |
| Wi-Fi | 保留消费者身份 | PENDING |

OV5640 → BSP DCMI M0/M1 → Media 的 Raw 七缓冲与 JPEG FIFO 是独立物理路径。Raw 使用原七缓冲，最多四消费者、各一块；归还按消费者、槽、实例和租约核验，DMA 轮换继续排除 IN_USE。JPEG 录像走原 FIFO SD 位、Session、Loan ID 和双 span，归还不通过 Raw API。同步文件写回到 StorageTask 时，调用方 span 的 CPU 访问已结束；SD IDMA 使用 StorageFS 自有输入缓冲，设备停止仍由卷释放独立确认。A3 USB Raw 与 V3.2B JPEG AVI 属于不同配置，当前未形成同一配置的 USB 与 Storage 双路视频产品，V3.3 独立退出测试验证服务依赖和保留行为。

录像使用 `STORAGE_SERVICE_RECORD_START` 打开可写采样；普通 `START` 仍只读。传入文件编号形成 `0:/XXXXXXXX.AVI`，建新文件成功后才写帧。触及单文件预算关闭准入、排空并收尾。停止中收到的旧 OFFER 仍按身份归还，失败文件保留首错，不生成成功收尾。配置采样速率不能替代实测。

## 业务协调和 IPC

Control 的软件 BOOT 完成后无需自动启动外设。显式 `AppV3Supervisor_Set` 计划启动时确认 Camera ACTIVE，再触发 USB 和录像。关闭时先确认不再需要的消费者终结，再停止最后无人依赖的 Camera。一路退出保持另一消费者及其 Camera 依赖。服务首错、等待超时和未确认退出位保留，故障路径可显式 `Recover`，已提交命令保留身份并等待匹配完成。Supervisor 每次只提交一条现有 Service 命令，状态取自匹配的 Business 结果；服务自主变化通过 `Query` 刷新最近状态。Network Service 待定义，依赖表没有占位业务契约。本次不增加外部命令协议。

现有 CM7 状态和 IPC Task 继续通过原共享 IPC 结构与 CM4 通信，`IpcApplicationStatus_t` 保持 80 B。OLED 新页面、CM7/CM4 新字段及用户独立集成均为 PENDING。当前报告不把软件快照当作 OLED 实际显示。

## 软件结果与资源

ARMCC5 5.06 update 7，两个优化档均通过。

| V3.5 回归 | O0 | O2 |
| --- | ---: | ---: |
| V3 Runtime / Service / Storage / Supervisor | 73/73 | 73/73 |
| A1 MediaLease，含真实 JPEG SD-only | 32/32 | 32/32 |
| A2 Dispatcher | 2/2 | 2/2 |
| A3 UVC Reader | 11/11 | 11/11 |
| A3 Raw Service | 8/8 | 8/8 |
| 原八组板测软件配置 | 50/50 | 50/50 |
| 本地 A4.1 编排，独立记录 | 5/5 | 5/5 |

软件回归合计 352/352，本地 A4.1 叠加另有 10/10。A4.1 无新增实板结论。阶段早期编译及夹具失败均保留日志；最终回归全部进程退出码为 0。AppMessage、ServiceCommand、IPC 布局分别为 52 B、16 B 和 80 B。

| Keil 完整 Rebuild | 结果 | Code / RO / RW / ZI（B） |
| --- | --- | --- |
| CM7 产品 MJPEG | 0 Error、0 Warning | 137326 / 3982 / 1292 / 101028 |
| CM7 A3 Raw | 0 Error、0 Warning | 139350 / 3982 / 1296 / 101024 |
| 原 CM4 | 0 Error、0 Warning | 26302 / 1438 / 936 / 3576 |

CM4 HEX SHA256 为 `3030484C03D4983A192BE39142DCE7B6DFDB8E6C93A49E1F8285EE846540FB17`，与冻结版一致。CM7 产品 DTCM 执行区 69,232 / 131,072 B，AXI SDMMC DMA 区 32,768 B，共享区 320 B。原静态栈按 Camera / USB / Storage / Control / IPC 分别为 6144 / 8192 / 8192 / 3072 / 2048 B，优先级为 3 / 4 / 1 / 2 / 1。运行期栈水位为 NOT_RUN。

证据目录为 `output/ApplicationV3/V3.5/`，最终软测在 `Regression-20261123-201013/verification.json`，Raw 重建在 `Keil-Raw-20261123-200945/`。产品 Map 与双核构建位于 `output/ApplicationV3/V3.3/Keil-20261123-200543/`。两次构建分别使用冻结产品 MJPEG 与独立 A3 Raw 宏，完毕后 Keil 用户工程和选项哈希均恢复为构建前记录。历史 output 与 A4.1 证据仍在原路径。

## 集中实板验收清单

- Camera 与 USB Raw 独立启停、Windows 实际协商及动态出图，记录 Camera 完成、设备 EOF 与主机接收。
- JPEG SD-only 录像、双 span、AVI 播放与索引、SD 满及拔卡、同步失败和显式 RECOVER，记录物理归还和真实写入帧率。
- Supervisor 局部退出、全局停止、队列背压、设备异常与不同运行配置的栈高水位。
- 用户接入 Network 和 OLED 后，单独验证 MAC/DMA/网口读者退出、IPC 兼容及 OLED 心跳，再验证其他路仍能继续工作。

所有以上未执行项均为 NOT_RUN 或 NOT_VERIFIED。A4.2 / A4.3 暂停。

## Git 记录

- `0ce8c7151d937a470f81c335a053c04234e89c86` V3.2B 录像与 JPEG Storage 所有权。
- `1874dca43447ded38d1f07c83e380d6828b33cff` V3.3 Supervisor 及 V3.5 验证使用的生产源码。
- V3.5 仅新增本报告。最终本地及远端 SHA 见 `output/ApplicationV3/V3.5/git-delivery.txt`。

仅交付开发分支，不合并 main。完成软件交付后进入集中审核。
