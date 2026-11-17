# EP0 Deferred Status 实施记录

基线：14f44ca678647421d3519f192d441724178a7882。范围：Platform usb_device.c/.h、专项测试，不修改 ST Core 或 BSP。

## VERIFIED：控制事务

Class Setup 期间只能登记与当前请求字段完全一致的 0x21、零数据长度请求；仅登记成功并返回 USBD_BUSY 才跳过原有 STALL。普通 BUSY、FAIL 保持原处理。错误地登记后返回 OK 的底层提前状态提交被拒绝。

Token 单调递增且不跨生命周期复用，UINT32_MAX 后拒绝新建。新 SETUP、Reset、Disconnect、Stop、DeInit 立即取消旧资格，硬件清理失败也不恢复旧资格。取消只撤销控制事务资格，不释放外部 DMA/Worker 缓冲。

任务 Complete 接口拒绝 ISR，验证 Token、请求身份、配置及 WAITING 状态。在 USB IRQ 锁内调用原 Core 状态发送，读取实际 LL 操作结果及端点 busy；不依赖 CtlSendStatus 固定返回的 OK。SUBMITTED 与 COMPLETE 分开，后者由实际 EP0 IN 回调确认。Fail 接口在任务超时后 STALL，不宣称外部 IO 已停止。

## VERIFIED：软件证据

- Tests/MscDeferredStatus/results.json：16 个场景全部通过，221 个 C 断言及 ISR 拒绝检查。执行真实 ARM Core/Platform/BSP，HAL 为 Mock。
- 旧 MSC 枚举集成：30 个检查通过；旧 Reset preflight：4 个场景通过，证明普通 OK/BUSY/FAIL 行为。
- CM7 MSC / UVC 完整编译均 0 Error、0 Warning。日志 output/MSC24/build/，RTE 自动生成项目名恢复，未改用户工程设置。
- current_regression_results.json 与 baseline_regression_results.json：10 套历史测试的每个 PASS/FAIL 行及汇总一致。历史失败保留，包括 UVC Streaming 35/36 失败、Payload 2/124 失败、Core Decoupling 3/204 失败，以及旧描述符测试缺少 camera_mjpeg_fifo.h include。不能称历史测试全部通过。

## NOT_TESTED / 限制

未进行本次固件下载、Windows 控制事务抓包、真实取消时序或 SDMMC DMA 实板验证。专项测试中的外部所有权是保留标记，不等同于真实 Worker 排空测试。后续 BOT 必须等待外部排空，继续检查 EP0 COMPLETE 和双端点 Clear HALT；不得仅凭本机制认为 BOT Reset 已安全。

## Git 边界

只提交 Platform、专项测试和本记录；保留用户 h7_mac.uvprojx/.uvoptx 未提交修改。无 ST Core、BSP、UVC、Camera、Media、CM4、MPU、链接或 RTOS 配置修改。
