# Stage08D 七缓冲与USB/SD双路租借

本轮替代554a1b3的SD整帧副本方案。开发分支codex/stage08d-jpeg-storage，不合并main。

## 数据流和所有权

DCMI/DMA直接写入JPEG字节FIFO，Media解析SOI/EOI并发布READY。CameraTask仍是Media任务侧唯一调用者。USB继续沿用FrameRequest → FrameOffer → FrameReturn → ReturnAck及epoch/lease校验；UsbTask和UVC协议未改。

Camera取帧时，Media为该帧占用一个在途槽并登记主引用。若SD订阅，Camera通过CameraMjpegFifo_RetainFrame增加SD引用，把两段原始地址、真实总长、帧元数据和凭据放入有界队列。Camera不复制整张JPEG、不扫描JPEG、不计算CRC；StorageTask借用原始地址进行验证、CRC与文件操作。

Media使用7条JPEG租借记录作为硬上限，正常生产路径保留一个完整帧描述余量。每张分别具有USB/分发端与SD所有权位，数据块引用属于整张帧，只有最后一路归还才释放。两路先后顺序均可。保留原USB一次一张在途的约束，七张并不表示七个USB事务同时在途。

```text
READY → 主引用（USB或SD-only分发端）
                  ├─ USB原租约 → 原Return/ACK → 主引用归还
                  └─ SD引用 → QUEUED → WRITING → RETURN_PENDING
                                                   ↓ CameraTask
                                              Media归还确认
                                                   ↓
                                            描述槽FREE（ACK）
两路引用均清除 → 数据块可回收 → DMA方可重新使用
```

SD只保留一个静态描述槽：FREE → RESERVING → QUEUED → WRITING → RETURN_PENDING → RETURNING → FREE。入队、写入、等待Camera确认归还都占用同一张，USB与SD可以各借一张，也可以共同引用同一张；不会排队持有多张待写JPEG。占用时WantsFrame返回false，不为了SD继续取得原帧。只传递描述符，不分配整帧副本。释放失败保留凭据和原帧，Camera后续重试；会话号和槽位generation防止重复或迟到归还释放新帧。StorageTask不能直接释放Media。

SD-only仍由原Bridge的Camera侧取还门执行，立即归还主引用，SD引用独立存活；不构造虚假USB消息。JPEG错误停止先确认硬件停止，再保留DRAINING；USB归还不能提前清理SD引用，最后一个SD引用归还后CameraCapture_Stop才能回到NONE/IDLE。

## 七缓冲与最新READY规则

固定长度YUY2保留7块实际缓冲。DMA两目标始终为DMA，Application借用者为IN_USE，至少保留一个FREE；剩余已经采集完成的缓冲可以保留为READY。尚未填满时FREE会多于一个。稳定无借用时为2 DMA + 4 READY + 1 FREE；USB持帧时为2 DMA + 3 READY + 1 IN_USE + 1 FREE。YUY2模式不保存SD JPG。

替换时优先使用多余FREE；只剩一个FREE则替换最旧的未借出READY。BSP切换确认成功后才提交状态；失败不修改映射、序号、统计或READY。ready_index只标识尚未交付的最新帧，其他READY保留作可回收空间。最新帧取走后，即使仍有旧READY，也不回退交付。丢帧只在旧READY被实际回收时计数，因此和旧的单READY模型相比统计值会变化。

JPEG的DMA管理对象是64个字节块，不是7个整帧缓冲。完整帧描述预算为7，其中保留一个接收余量，已发布READY与借出帧合计最多6张；USB、SD各最多持有一张。取JPEG时只选择最新READY并丢弃更旧READY的引用，既有租借不受影响。七条租借记录是硬上限，不能把字节块的FREE数量与整帧描述余量混为一谈。

## 内存布局

现有BSP声明芯片容量64MiB，FMC几何配置未改。业务MPU窗口从4MiB改为8MiB，仍为Non-cacheable、Shareable、禁止执行。高地址真实读写仍需实板验证。

| 范围（结束地址不包含） | 用途 |
|---|---|
| D0000000..D0000400 | 原1KiB保留区 |
| D0000400..D041A400 | YUY2等固定长度模式，7×614400B，均32B对齐 |
| D0480000..D0580000 | JPEG FIFO，1MiB，64×16KiB |
| D0580000..D0588000 | 两个16KiB丢弃目标，空间不足时保持DCMI连续 |
| D0590000..D05A0000 | AVI索引4096×16B |
| D0800000 | 8MiB业务窗口结束 |

YUY2每块600KiB是容量预留，实际传输长度仍取决于配置；增加数量不会绕过DMA65535 Word上限或开放高分辨率YUY2。JPEG块数保留64及原uint64_t块位图，单块改为16KiB，DMA每次4096 Word，FIFO/INC4硬件配置未改，正常帧间不暂停DCMI。

原SD副本池D0384000等地址不再使用。SDMMC仍使用24000000处32KiB AXI IDMA缓冲，SDRAM原帧经过既有块设备中转后写卡；USB仍经过Payload/PIO。因此这里只消除整帧副本，不能称完全零搬运。1MiB字节容量也不是7个无限长度帧槽；无空闲块时原丢弃机制继续运行，绝不覆盖租借帧。

## 存储与接口

App_StorageOfferFrame成功只表示取得独立SD引用并入队，不表示写入完成。App_StorageProcessReturns只由Camera调用；App_StorageProcess仍只由Storage调用。GetStatus只是诊断副本，不是释放授权。

Storage逐段写入原始JPEG；跨FIFO末尾不拼接。JPG写完关闭后重开，按原两段进行长度/内容/CRC核对。AVI新增AppendFrame，把两段写进同一00dc Chunk，索引、EOF、尺寸回填和正常封尾语义保持不变。写入函数返回后，SD IDMA若仍有故障仅持有原AXI中转区，不再访问原SDRAM帧；文件系统恢复与原帧归还是两条独立生命周期。

保留静态Ops+Context接收端接口，现名称FrameSinkService_t；Storage统一事件分发和状态转换保持原设计，不引入完整Furi Record或SceneManager。

## 用户当前视频配置与IPC

用户保存：320×240 JPEG，目标输出25FPS，UVC最大40000B；非MJPEG仍160×120 YUY2。40ms最多40包，每包1023B含2B头，40000B需要40包，带宽校验保留；70000B/25FPS仍拒绝。超过UVC上限的JPEG不截断，按原USB拒绝/归还逻辑处理。采集时序和传感器FPS未修改，目标不等于实测。

IPC_HEARTBEAT_PERIOD_MS仍100，MISS_LIMIT改10，失联超时1000ms。IPC_APPLICATION_FRESH_MS仍500ms，业务数据过期与对端失联不同。两核需要配套编译烧录。

## 仍待实板确认

本轮没有实板烧录、写卡或CPU占用测量。此前实板已确认抓拍文件写入/回读CRC一致，同时Camera上报DMA FE导致协调停机；不能据此宣称新的租借方案已消除FE。

用户反馈：不开播放器、复杂画面时OLED偶发CHECK IPC LINK。此时不能直接归因USB视频发送。优先观察CM4 peer_state/peer_age_ms/protocol_errors、CM7 max_step_gap_ms/scheduling_delays/wake_count。高优先级Camera解析任务饱和是待验证可能性；增大超时不解决调度饥饿。扩大DMA块使单轮解析预算也变大，复杂画面下的CPU负载、解析延迟与FIFO丢帧必须实测，本轮未擅自调整任务优先级或调度机制。
