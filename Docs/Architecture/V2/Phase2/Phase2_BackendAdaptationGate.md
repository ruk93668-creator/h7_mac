# Phase 2 真实Backend适配门禁（未实施）

本轮只实现Resource软件基础和受控端口测试。以下是后续最小必要差异建议，未经单独批准不修改Legacy/Media/Bridge/Storage或原工程。

1. camera_capture.c内部Raw Stream已有M0/M1、READY、IN_USE及帧序号等事实，但公开StreamGetFrame/StreamReleaseFrame无完整身份查询。拟新增只读、临界区一致的Raw持有快照，提供本次源实例/slot/真实序号及DMA排除事实；如缺少不可复用代次，先单独审查字段/ABI/生命周期边界，不能自行造一个Grant号冒充物理token。不得新建池或改rotate/停止时序。
2. camera_mjpeg_fifo.c的loan session/id及usb_owned/sd_owned为真实权威，目前缺只读特定token快照公开口。拟提供已有账本的只读查询及原始/SD持有区分，不创建Loan、不Retain、不Release、不改块引用算法。M字段必须来自真实账本；不得用Frame地址推断。SD-only原始用途必须显式标内部，不伪标USB业务G。
3. Bridge需在原D/T适配边界发布匹配ACK消费证据（不是只有入队），Camera owner据原B及G/H映射确认T。Storage须从原slot/session/generation交接账本及Camera实际ReleaseLease取得证据。只读快照不能拼跨任务瞬时状态得退出结论；所有Get/Retain入口必须经同G关闭闸门。具体同步字段/临界区/最小文件Diff须另审。
4. DMA端口必须逐一对原始读取者证明完成/成功Abort及禁止新读者；不能以驱动API返回0或全局busy快照冒充。Raw采集目标、JPEG块写目标及UVC逐包读取/SDCPU复制各按已有实现核对，新的Resource不控制硬件。

兼容性要求：新增查询只读；原ABI、返回语义、PRIMASK嵌套/恢复、屏障、Get/Retain/Release次数、DCMI/DMA/USB/SDMMC时序不变；须真实旧模块联合O0/O2及真实板故障验证。没有这个适配不得调用MR_VerifyExit的Mock成功作为资格RELEASE依据。Source ops是可信契约，不是任意返回SYS_OK就可绕过资源证明的实现。

真实适配、任务消息/ACK发布、实际DMA、完整72/32场景均NOT_RUN。两项总体NOT_PROVEN保留；这不是Phase3 Execution Manager授权，也不是提前删除Legacy/更改Keil的许可。

## B分配时点：Resource内部顺序兼容已修正，真实接入未实施

真实app_uvc_bridge.c:241～255先CameraCapture_StreamGetFrame，再Publish给Storage，最后分配offer.lease_id。最终Resource允许Stage只有请求ticket，Admit/Obtain后调用MR_BindBridge绑定实际B，再发布Offer；MR_LookupBridge反查原请求与M。此修改仅在本轮允许的Resource文件中完成，测试52～55通过，不改变Legacy分配点或硬件调度。无帧负向终结和Get后未发布便Close均不伪造B。

真实Bridge适配仍需单独批准：将G/H受保护发布、实际M查询、B绑定/查找、终结消费与旧传输关联；禁止重复Get，禁止以当前G重建旧消息身份。

另一个尚未完成的软件适配是旧52B请求/ACK到完整G/H/T的受保护元数据发布及消费回传。MR_Stage/Commit顺序调用不能代替抢先消费竞态证明。B/S的线报文非复用与旧报文匹配仍属此适配，现单测采用递增模拟B/S身份，仅验证完整本地请求身份，不能据此声称旧线协议ABA已解决。该部分仍BLOCKED/NOT_RUN，两项总体NOT_PROVEN不变。

ACK语义待审：本地T.result为冻结操作错误，physical_returned为历史物理归还事实（受信任Backend确认后才可置位）；Legacy帧ACK与原STOP命令IO分别映射，不直接把T.result复制到旧帧ACK。Source状态查询失败/部分归还场景必须逐轨迹核验。
