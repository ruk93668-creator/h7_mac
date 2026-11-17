# MSC-4 受控 SDMMC 接入

状态：VERIFIED 软件接线与Mock；NOT_TESTED 全部真实SD操作。本阶段不实施MSC-5动态归还，未执行真实写卡。

## VERIFIED：分层与执行位置

usb_msc_class → BOT → SCSI计划 → backend接口 → usb_msc_storage单槽邮箱 → StorageTask → usb_msc_sd → SD_Block_* → 既有SDMMC BSP。

StorageTask BOOT在受控编译模式调用USB_MSC_StorageInit(USB_MSC_SdDevice())，后续周期及通知仅执行StorageProcess；不调用App_StoragePrepare/Process。阻塞Read/Write/Sync全部在StorageTask，USB ISR和UsbTask都不等待SDMMC。

新增SD_Block_GetIoDiagnostics提供持租约任务的诊断副本，保存部分完成块数；未更改原读写算法、BSP、MPU或scatter。SD失败即锁定设备且不重试写入，仅使用与本次LBA/count/方向/result匹配的诊断；恢复前保存completed_blocks，早期失败不伪报成功。adapter/BOT保留原始结果及确认完成量。

## VERIFIED：受控卷门禁

msc_test_config.h默认USB_MSC_SD_TEST=0、USB_MSC_SD_WRITE_ENABLE=0。SD测试必须同时显式USB_MODE_BOOT_MSC=1，否则编译拒绝。SD测试模式下：

1. SD_Block_Claim(FATFS)始终拒绝，RAW租约仍绑定StorageTask身份。
2. SD_Block_FatFsAllowed返回false；StorageFS_Mount和所有文件操作共用Storage_Enter在调用FatFS前拒绝，即使状态被错误置为MOUNTED也拒绝。
3. StorageTask跳过本地文件业务；初始化失败也不会回退FatFS。
4. RAW租约从不因USB Reset、Stop、Disconnect、SCSI Eject或SD故障释放；不自动挂载、不格式化、不自动重新初始化卡。
5. 缺卡、掉卡、CID异常或IO失败锁住ready；重新插卡不自动解除锁定。介质代次改变交由SCSI报告Unit Attention。

这是一种固定受控启动模式，不是完整FAT所有权状态机。USB Token取消与DMA所有权独立。

## VERIFIED：容量、IO及恢复

识卡后要求512B逻辑块、容量非零，比较CID快照。每笔最多2块，检查LBA减法边界；BOT连续多笔完成长命令。Read/Write调用既有同步API，超时5000ms；其成功路径已等待硬件静止、DCOUNT归零及卡TRANSFER。Sync核对该同步契约后的状态，不宣称刷新PC尚未发送的缓存。

IO返回后仍必须device.quiescent。诊断active非零时，只调用一次现有SD_Block_Recover，并重新确认active==0；失败继续保留DRAIN及buffer，无无限阻塞等待循环，不把超时视为DMA停止。BOT Reset限时2000ms，故5000ms IO未完成时可能拒绝控制事务并锁住；后续只有真实排空后才能再次尝试Reset。

## VERIFIED：DMA / Cache / Buffer

- BSP源码与链接map确认g_sdmmc_dma_buffer在0x24000000，32768B、32字节对齐，每批上限64×512B。MPU Region3为non-cacheable/non-bufferable/shareable；未修改。
- MSC数据缓冲1024B、32字节对齐；USB FS采用PIO，DMA关闭，BSP按64B复制至PIO池并保留发送上下文至完成。
- SDMMC IDMA只访问既有AXI专用区，不直接访问BOT的DTCM缓冲；BSP在Task中复制CPU输入/输出。CPU、USB及Worker按状态交接，不能双重持有。
- 上述路径无需对BOT DTCM或非缓存AXI做DCache Clean/Invalidate。若未来打开USB DMA、更换内存区或做零拷贝，当前结论不再成立，必须重新审批。

## VERIFIED：软件验收

Tests/MscSdIntegration：只读24场景、编译启用写分支的Mock24场景均通过。包含512B几何、容量尾块、非法范围、写保护、Sync/Eject、诊断部分失败、恢复成功/失败、掉卡后锁定、CID变化及旧代次。没有访问真实硬件。

run_gate.py直接编译真实SD_Block/StorageFS，模拟已准备的RTOS同步资源；9个断言证明FATFS Claim、只读/可写Mount及伪造MOUNTED后的文件入口都拒绝，而RAW租约成功。未预期BSP/FatFS调用使用陷阱桩，若误调用会使测试失败。

Tests/MscJoint：真实USB Core/Platform/BOT/adapter与Mock设备，5场景验证执行中BOT Reset、Bus Reset、Disconnect、新SETUP取消以及部分写入512B residue。这里只模拟抢占调用顺序，不是实际多任务/IDMA测试。

## NOT_TESTED / UNRESOLVED：必须人工补齐

- Windows真实SCSI/BOT、USBPcap、RAM LUN单/多块校验。
- 专用可销毁卡：先容量/只读、跨32KiB及尾块读，再经人工明确确认才编译并下载写测试模式。
- 实际多块写+Sync+读回、写中拔线/拔卡、SD超时/Abort失败、USB Reset及晚到IRQ。
- 卡检测引脚极性与V1板适配、实际任务栈余量、长时间吞吐和稳定性。CID来自现有API快照，漏采物理拔插的辨识能力仍需实板验证。
- OUT晚到IRQ隔离依赖现有HAL停端点和直接ISR分发；并未新增硬件OUT序号接口。软件测试只覆盖取消/排空期间事件，尚不能替代真实HAL IRQ竞争验证。真实写入验收前必须补齐。

不发布/下载可写SD实板固件；当前生成的sd_readonly构建保持写保护。UVC实板仍受既有V1板SCCB故障阻塞，未改Camera/BSP。
