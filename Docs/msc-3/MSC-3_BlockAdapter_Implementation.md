# MSC-3 异步Block Adapter / Storage Worker

VERIFIED：usb_msc_storage.c/.h实现单槽有界任务消息邮箱，无堆分配。UsbTask通过backend submit/poll调用；StorageTask通过StorageInit/Process执行设备接口。QUEUED→RUNNING→DRAIN→DONE→EMPTY，状态修改使用保存PRIMASK的短临界区与屏障，所有阻塞IO在锁外。通知仅唤醒，通知失败仍可定期轮询。

VERIFIED：请求携带transport epoch、单调block token及media generation；提交核对介质、几何、最多2×512B、写保护、队列容量与Token不复用。poll精确匹配三身份。完整/部分完成结果单独返回，不放大completed_blocks。媒体仅由Worker刷新，USB读取快照。

VERIFIED：取消QUEUED可立即退役；RUNNING/DRAIN只记录取消意图，直至device.quiescent确认才能退役或发布完成。cancel不清DMA Busy，不改请求内buffer。发生Reset时旧请求仍独立存在于邮箱，BOT排空后才能复用其静态缓冲；迟到完成不会自动重新启用USB。

VERIFIED：Tests/MscBlockAdapter/results.json共18个Mock场景通过，覆盖初始化失败、容量1背压、异步执行、queued取消、在execute中模拟Reset取消+硬件继续活动、三身份错配、部分失败、换卡代次、ISR拒绝、写保护、越界、通知失败与新旧epoch。此测试编译真实adapter，设备和同步原语为Mock。

INFERRED：现有StorageTask受控分支为唯一真实设备调用者；MSC-4接线前不激活该worker，不增加任务或改变FreeRTOS配置。SD真实任务身份由既有SD_Block租约再次强制核对。

NOT_TESTED：真实SDMMC取消、任务栈水位、实板队列/IRQ竞争及吞吐。安全失败会保持DRAIN，不能以超时释放buffer。阶段独立提交；下一阶段加入受控SD门禁。
