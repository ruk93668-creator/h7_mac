# P8.3-A API 决策登记

源码 e0ad3b5744b73095f09b3609738e7431956082c6。以下是候选评审项，新增接口与硬件授权均待人工评审。

| 决策 | 现有生产源码与风险 | 推荐最小适配 / 状态 |
|---|---|---|
| A01 权威 G/R | V2 usb_consumer.c:48–167、storage_consumer.c:43–169 使用共享 Generation 与现有 Tx；v2_rtos_runtime.c:231–247 拒绝真实 Lifecycle。旧资格已归档 | 由同一 Camera owner 绑定来自原始 ACQUIRE 的完整 G 与独立 RELEASE R。不能从 B/S、Debug 或地址推导。NOT_PROVEN |
| A02 Raw M | camera_capture.c:1107–1237 由 Bridge 经公开 Get/Release；唯一 in_use 不足以跨复用识别 | 最小只读物理 token 和匹配验证，保持单持有和原 DMA 接口。NOT_PROVEN |
| A03 JPEG M | camera_mjpeg_fifo.c:325–437、840–866，真实 session/id、owner bits；legacy_index 只代表当前原持有 | 提议暴露匹配的 loan 信息，保留双 span 和既有引用计数。NOT_PROVEN |
| A04 准入和关闭 | app_uvc_bridge.c:217–263 两个 Get，app_storage.c:237–330 Retain；没有 G/R 屏障 | 必经同 Camera owner 的 MR Admit/Close，覆盖 SD-only；关闭后未取得 M 的 H 不得再获取。NOT_PROVEN |
| A05 USB async | usb_uvc_class.c:315–472、1063–1117、bsp_usb.c:337–369，INVALID_STATE、reset 与迟到 IRQ 可被误作终止 | 绑定 packet/token/端点事实至对应 B/H/M，独立取消、正常 EOF、reset/deinit 验收。NOT_PROVEN |
| A06 Storage I/O | app_storage.c:334–375、988–1007，BSP Sdmmc_Transfer/StopIo；同步写返回与 IDMA quiescence 不等价 | 复用 S 和 ReleaseLease，分别查询原帧读者、IDMA、真 M 已释放，不造物理计数。NOT_PROVEN |
| A07 消费确认 | app_uvc_bridge.c:278–287、432–478；media_resource.c:535–621；P8.2 V2Rtos LC consume 独立 | 用既有模块通信边界确认 T 的真实应用消费，形式与容量待评审。保留满队列背压和原退休规则，52B/56B 不动。NOT_PROVEN |
| A08 最后停止 | app_camera.c:113–154、355–395；camera_capture.c:695–835；VCamera_QueryExitEvidence | 第一条 G 完成不停止另一路；待全部 G、内部持有、未决 T、Media/DMA 和首错证据满足才由原停止点推进。NOT_PROVEN |
| A09 原合同 | app_camera_lifecycle_replay.c、app_camera_generation.h 与 H7_V2_ModuleContract 的 B2 ID23/ID31 | 不新增固定 N 次 Replay 配额或退休策略，保留合法后继、Generation 溢出、ID23 有界 BUSY、ID31 合法成功 START 才复位。后续验证 NOT_RUN |

继续冻结五核心架构、52B/56B、Replay、任务优先级/栈/IRQ、DMA 算法、OLED 延时、IPC V3。任何建议均不允许在证据缺口时开放 RELEASE、STOPPED 或 IDLE 成功路径。
