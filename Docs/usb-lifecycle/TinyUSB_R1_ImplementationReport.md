# TinyUSB R1：部分实现交付 / BLOCKED

VERIFIED：起点8a0a89cf49dfb86c9126d3c3193abc23e8c608e9，分支codex/usb-tinyusb-p1-replacement。未改旧UDLM、main、Keil、SDMMC或FreeRTOS；未烧录、不启用生产替换路径。

**未达到R1完成标准。旧六项无界等待没有消除，不能宣布Safe Abort成功实现。** 本次实现的是三态接口、Core/MSC失败传播和DWC2保守拒绝路径，不是完整停止原语。

## 实际代码

VERIFIED：dcd.h新增DCD_ABORT_UNSUPPORTED/SUCCESS/FAILED和dcd_edpt_abort_safe。Core弱默认适配原bool API，保留其他DCD原兼容回退；仅UNSUPPORTED可回退。usbd_edpt_abort改为bool；SUCCESS才清BUSY/CLAIMED；FAILED锁存不可恢复的device fault，返回false，不调用STALL/Clear-Stall。

VERIFIED：Core故障后拒绝Open/Claim/Release/Xfer/ISO Activate、Clear Stall/Close、Reset事件及DeInit/Init，保留旧instance。MSC两个abort调用检查返回值；proc_bot_reset变为bool，失败保持原stage，不发送成功状态包。不建立StopAttempt或新Manager，不清锁存重试。

VERIFIED：DWC2新增强实现只对合法非零端点启动保守停止：IN提交SNAK，OUT提交Global OUT NAK；保留xfer_status指针并返回FAILED，不作循环，不把EPENA=0当proof。当前实现**不会返回SUCCESS**，也不支持故障后恢复。该入口不是已完成的Bulk取消。为控制终态中断风暴，明确GINTMSK=0屏蔽控制器事件源；这不是硬件静止证明，不释放Buffer。新的DCD提交、连接、关闭/初始化及IRQ入口在该fault下拒绝执行。原控制器/RAW引用保留。

UNRESOLVED：这是全控制器终态隔离，不能作为可继续服务EP0或其他端点的取消。失败时首要保证不进入旧fallback；需要共享RX水位、旧XFRC归属和真正停止证明后才能提供SUCCESS及恢复。当前没有宣称硬件超时：单次发布后证据不足立即失败，非把循环次数当时钟。

## 调用者兼容性

VERIFIED：本地选入src的usbd_edpt_abort生产调用者只有MSC IN/OUT两处，均已改为检查bool；usbd_pvt.h声明同步修改。新的safe API由Core调用；原dcd_edpt_abort_xfer bool声明/weak函数保留，旧测试DCD仍走兼容路径。原MSC12项通过，证明本实验旧fallback协议行为未退化；不代表所有上游移植端口二进制ABI测试通过。所有调用者须重新编译，void→bool不是旧二进制兼容保证。

BLOCKED：STALL、close_all、ISO、DeInit及EP0 ISR原void路径尚未统一成可传播失败契约；DWC2 common.c/.h未改，旧while仍在。没有只给void加return后假成功。这些路径在第一次新abort故障之前依然可能挂死，不能用本轮锁存掩盖缺口。

## 交付

实际生产Diff：Tests/TinyUSBReplacement/reports/R1_production.patch；路径/源码哈希、调用者和patch SHA见R1_audit.json。测试及未完成项分别见TinyUSB_R1_SoftwareVerification.md和TinyUSB_R1_RemainingRisks.md。原证据保留在历史Git及*-history报告。最终本地/远端SHA以真实交付回执和最终答复为准。

本轮以部分实现停止等待审核，不具备硬件或合并资格。
