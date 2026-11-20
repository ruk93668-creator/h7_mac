# P1-FINAL：资源账本审计

VERIFIED（真实源码，起点行号）：资源不是一个全局busy；USB端点静止与Storage IDMA静止分别证明。

| 资源 | 创建/持有者 | 停止请求 | 静止证明 | 最终归还 |
| --- | --- | --- | --- | --- |
| Controller/Session | BSP Init/Start；Platform Core | 原UsbTask/USB Mode；ISR只失效资格 | 全EP、EP0、消费者、FIFO票据匹配 | Platform/BSP；不能在失败时清started |
| IN Transfer / TX PIO | bsp_usb.c UsbTxTransfer_t、s_bulk_in；Transmit借源和固定包槽 | BeginBulkInStop | NAK/Disable、AHB/FIFO、旧IRQ、PIO引用、同Attempt Commit | BSP归还源/槽；Platform对应sequence清busy |
| OUT Transfer / RX Buffer | bsp_usb.c Usb_OutArm；HAL唯一RX消费者 | BeginBulkOutStop | 同lease marker水位、真实EPDISD、PIO=0、旧事件退休、NAK合法退出 | CommitBulkOutRetirement；不Flush共享RX |
| Global OUT NAK | Usb_OutClaim；Bulk/ISO共享单lease | 软件参与者登记位 | 硬件marker/GONAKEFF/GONSTS与唯一owner连续性 | 最后owner发CGONAK；观察真实解除后清lease |
| EP0 SETUP storage | PCD永久Setup数组 | 不参与单EP EPDIS | 同一次pop/copy状态；不能用数据Buffer替代 | PCD全生命周期；不提前释放 |
| EP0 DATA/STATUS/Token | Core/Platform；Class静态reply | 新SETUP/Reset/Disconnect失效控制资格 | 协议Token + 独立硬件读写/事件proof | 缺少完整契约：BLOCKED；不得以Token失效归还Buffer |
| MSC CBW/Data/CSW | usb_msc_bot.c:19 静态s_bot；backend借data | BotReset/BotStop | 双向USB proof + backend.drained(epoch) | BOT；旧请求身份保留到真实完成 |
| Storage request / SD IDMA | usb_msc_storage.c:49 Submit→StorageTask:125；SD execute | Cancel:86仅标记；QUEUED/DONE可退 | DRAIN时device.quiescent()才DONE/EMPTY（:156） | StorageTask；USB取消不能代替 |
| RAW SD lease | usb_msc_sd.c:14 MscSd_Init→SD_Block_Claim RAW | 本轮不交给FatFS | SD_Block诊断active=0；fault恢复只一次（:105） | 当前RAW一直保留，不因USB断开归还FatFS |
| UVC source frame | USB_UVC_SubmitFrame→Payload frame引用 | CancelFrame/Alt0/Stop | ISO XFRC/Incomplete/Disable/FIFO proof + Payload/Bridge消费者退出 | UVC ReleaseFrame后Bridge Return/ACK，再Camera owner释放 |
| Class fixed memory | usb_device.c:92固定1024B | ManagedClassDeInit | PreparedTicket全集 + Class成功DeInit/Free意向 | Platform真正提交回收；不能仅信Core同步返回 |

VERIFIED：MSC Storage mailbox以epoch/token/generation匹配；Cancel不清RUNNING/DRAIN。MscSd_Quiescent调用既有诊断/有限Recover而非把超时当停止。SD只读配置不改，不释放RAW、不更改FatFS。

UNRESOLVED：当前没有Controller统一ledger串联以上票据；现有正常路径不能直接宣称等于目标契约。UVC数据面及Camera/Media不改；仅USB侧提出停用和等待Return/ACK，不能新增旁路Frame释放。

## 最终增量核对

VERIFIED：当前C2只聚合端点proof，不建立完整Controller ownership ledger；多个端点退休也不授权Class、Worker、Frame或EP0释放。MSC DeInit新增backend守卫（usb_msc_class.c:94 / usb_msc_bot.c:240）避免取消未drained即Close。NOT_TESTED：实际SD IDMA/Frame引用及跨Session硬件迟到事件。所有缺少proof的域继续保留，最终状态BLOCKED，不把资源表设计当实现。
