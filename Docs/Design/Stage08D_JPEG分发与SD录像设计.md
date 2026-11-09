# Stage08D JPEG多路分发和SD录像设计

## 范围与基线

基线41a4ba4405d1a9eae5c84f9c9764432f3d052d76，分支codex/stage08d-jpeg-storage。只实际支持USB和SD，Ethernet/Wi-Fi/AI明确返回不支持；不实现网络、模型、JPEG解码或录像掉电事务恢复。

## 架构与唯一所有权

OV5640/DCMI/JPEG FIFO → CameraTask → Camera侧Bridge的唯一Media取还门 → Dispatcher → USB原帧租约 / SD副本服务 → StorageTask → AVI/JPG → StorageFS/FatFS → 原SD_Block/BSP IDMA。

app_frame_dispatcher管理本会话Frame ID、Timestamp、Epoch和静态副本服务。FrameCopyService以wants/offer+context绑定存储服务，借鉴Furi Record的能力访问思想；没有字符串注册、动态容器或无限等待。当前USB仍是原Bridge适配，网络选择在配置验证入口明确拒绝，不宣称已经可替换成实际网络发送。

Bridge Camera侧仍唯一调用Media Get/Release，SD不能另开Media消费者。USB有请求时，先登记持帧、复制SD副本，再走原Offer；SD失败不取消USB借帧。无USB请求且存储需要帧时，使用相同held记录作local_only租借，复制后归还，不生成伪ReturnAck。Release失败保留held及重试。USB侧Process、SameLease、UsbReturn原代码保持不变，租约三元组仍是epoch/lease_id/command_id。AppMessage只追加frame_id和timestamp_ms，配套M7固件内部重新编译，不影响SRAM4 IPC协议。

每帧元数据保存实际长度、尺寸、Frame ID、Epoch、时间及CRC。Timestamp是CameraTask取得完整帧的本地毫秒时间，不是曝光或硬件FRAME时间。元数据用于交接和诊断；本版不向JPEG插入EXIF，不在AVI写自定义逐帧时间戳。不能据此声称精确保留原始采集时序。

## 主输出和SD用途

VideoConfig_GetDefault选择互斥主输出USB UVC/ETH/Wi-Fi/NONE，原USB TEST仅作既有控制面兼容。NONE保持USB断开，但Camera流为SD抓拍/录像待命；NONE加SD OFF仍可能后台采集，本版不实现按消费者自动开关传感器。TEST不启动Camera，保存API拒绝TEST或非JPEG，避免伪JPG。

SD用途通过请求选择OFF/VIDEO/SNAPSHOT。初始OFF，不识卡、不挂载、不写入。VIDEO和SNAPSHOT活动时互斥，冲突命令在存储任务处理时拒绝并计数，不改变当前会话。USB断连、Alt0不自动停止SD录像；若要停止整个Camera，先停止Storage并确认正常封尾，再走原Control停止协议。运行中主输出热切换不支持。

VideoConfig_GetStorage是独立SD默认策略：目标10FPS、分段4MiB或60秒、总16MiB、最多16段、保留512KiB。录像目标不改变传感器或USB FPS。实际Frame ID及目标采样采用完整帧，采样受输入节奏影响，可能低于目标，必须实测。

## 队列、缓冲和并发

四槽有效容量70000B，步长70016B，32B对齐；静态帧队列4项，独立命令队列4项。没有动态堆。

| 区间（结束不包含） | 用途 |
|---|---|
| AXI 0x24000000..0x24008000 | 原SDMMC 32KiB IDMA缓冲，未改 |
| SDRAM 0xD0300000..0xD0380000 | 原512KiB JPEG FIFO，未改 |
| SDRAM 0xD0380000..0xD0384000 | 原两块丢弃接收块，未改 |
| SDRAM 0xD0384000..0xD03C8600 | 四个独立JPEG副本，280064B |
| SDRAM 0xD03D0000..0xD03E0000 | AVI索引，4096×16B |

原YUY2五块600KiB缓冲结束在JPEG FIFO之前。上述范围已在现有4MiB Normal/Non-cacheable/Shareable窗口内，不改MPU、Cache、Scatter和SDRAM初始化。CPU访问副本/索引，SDMMC仍通过原AXI bounce buffer及原Cache协议搬运。编译期断言保护窗口、对齐及不重叠，实际Map/ELF另做审计。CPU读JPEG FIFO并写副本会增加SDRAM总线负载，软件Mock不能证明并行性能不变。

槽位FREE→COPYING→QUEUED→WRITING→FREE。复制在原帧租借有效期内，使用真实size_bytes-size_bytes2和size_bytes2拼接；第二段可完全不连续。验证和CRC不在临界区进行。停止期间正在复制的槽位在提交时发现入口已关闭便回收，不能入下一会话。queued副本携带session和generation，过期或重复释放不能释放新槽位。池满直接丢存储帧，不阻塞USB、不覆盖WRITING。

## StorageTask与SceneManager思想

StorageTask静态栈2048 Words，优先级1，登记第五个BOOT目标。原Camera/USB/Control/IPC优先级及栈不变。app_tasks和RUNTIME_TARGET_COUNT一致更新为5；Scheduler还会创建Idle，本数字只统计登记业务任务。

事件统一进入Storage_HandleEvent，按当前状态决策；文件操作与状态提交分开，状态唯一存于私有s_state，Storage_Transition是唯一状态写入入口。Debug仅镜像观察，不参与资源所有权判断；查询API读取完整本地发布副本。没有通用SceneManager、场景栈或on_exit自动释放资源。

| 状态 | 请求/事件 | 行为 |
|---|---|---|
| IDLE | VIDEO/SNAPSHOT | Storage任务挂载、检查空间，建立新session后开放复制 |
| VIDEO/SNAPSHOT | 冲突开始请求 | BUSY，当前会话不变 |
| VIDEO/SNAPSHOT | STOP | 关闭新复制入口，进入DRAINING |
| DRAINING | 副本仍在队列 | 继续写完已入队副本 |
| DRAINING | 队列及COPYING为空 | 正常封尾、同步、关闭、卸载，成功才IDLE |
| 任意活动状态 | I/O失败 | 停止接收，回收未写副本，记录主错和清理错，进入FAULT |
| FAULT | STOP | 保留故障，不假装已恢复 |
| FAULT | RECOVER | 先读取08C诊断：真实故障租约执行Recover，仍挂载则重试关闭/卸载，已READY只解除应用故障；成功才IDLE |

没有新完整帧超过5000ms时结束存储等待并记录TIMEOUT，不操作Camera或USB恢复。Task阻塞等待通知；100ms超时用于人工业务请求和无帧检测，不在超时轮次自动访问卡。队列尚有工作会自通知，不依赖100ms轮询排空。栈扫描最多每1000ms一次。

## 文件保存与AVI

抓拍：CREATE_NEW IMG00001.JPG，最多128次重名搜索、编号最多99999，不覆盖、不自动删除。分批最多32KiB写入，检查实际字节数；sync/close后重开逐字节比较、CRC、真实长度，源副本已校验JPEG结构。失败不计成功，可能留下不完整文件。电脑独立解码仍是必需硬件验收。

录像：AVI 1.0 RIFF AVI / LIST hdrl(avih,strl(strh,strf)) / LIST movi(00dc JPEG帧) / idx1。显式小端，JPEG奇数长度补一个零字节；chunk长度不含补齐；idx1第一帧offset=4，相对movi FOURCC，JPEG标记关键帧。头224B，单项索引16B，最多4096帧。接收前把数据、Chunk、索引均计入分段预算。达到字节、时长、索引、段数或总量上限时有界切段/结束。固定帧率AVI在正常完成时根据首末实际接收时刻加一个名义帧周期回填平均rate/scale，不补重复帧、不伪造目标FPS；不能保留逐帧间隔抖动。只有一帧时沿用名义周期。

每约1000ms在低优先级Storage任务同步；正常停止写索引并回填头、同步、关闭后才增加saved_files。saved_frames是已成功写入帧数，不保证当前开放AVI已经完成可播放。最终失败文件不计成功文件。

容量：挂载后检查实际FAT卷空间，保留512KiB；预留不足时正常封尾，不主动写到介质报满。当前08C实板卷为8MiB，不把7.8GB整卡容量当成该卷可用空间。文件系统实际短写/掉卡可能留下不能播放的文件；保留错误，不自动格式化、删文件或声称能掉电恢复。固定单段上限256MiB、总量上限1GiB，避免32位运算溢出。

## 接口返回与诊断

StartRecording/RequestSnapshot/Stop/Recover返回SYS_OK只表示命令入有界队列。开始请求可能随后被拒绝，应用必须查询状态和拒绝/失败计数；没有跨任务直接调用FatFS。Stop成功入队立即禁止新副本，但完成须等待state=IDLE且result/cleanup_result均SYS_OK。停止不删除已接受副本；故障恢复不是原文件保存成功证明。

g_storage_debug提供ready/request/state/mode/session、saved_frames/files、dropped_frames、rejected_requests、queue_high_water、max_copy_ms、max_write_ms、last_size、last_crc、read_crc、最后帧元数据、measured_fps_milli、stack_free_words和文件系统诊断。max_copy_ms包含校验/复制/CRC及被抢占时间；max_write_ms包含本轮文件创建/写入/验证/切段耗时，均为毫秒分辨率，0不代表零耗时。Read CRC只对JPG回读适用，不代表AVI已经回读校验。

AI接口目前返回NOT_SUPPORTED；未借出Media指针、未运行模型或解码。

## 发布和恢复的补充约束

共享槽位与队列使用volatile、短PRIMASK临界区及DMB保护，多字段不能只靠volatile。队列显式携带slot/generation/session，三者匹配才取得WRITING。文件系统确认安全、没有复制或I/O后，才能回收故障遗留的独立副本，不触碰Media或USB。

预留空间是应用预算，不是磁盘配额。FAT簇取整和目录占用可能使真实余量不同，真正写满仍依赖部分写/错误检测；失败末段不保证可播放。新SDRAM地址的实板读写和并行总线性能待验收。

AVI关键偏移：RIFF长度4、hdrl LIST12、avih24、帧周期32、HASINDEX44、总帧数48、宽高64/68、strl LIST88、strh100、vids/MJPG108/112、scale/rate128/132、流长度140、strf164、BITMAP头172、宽高176/180、planes/位数184/186、MJPG188、最大JPEG192、movi LIST212及长度216、movi标识220、第一帧224。idx1相对movi首帧偏移4，数据奇数长度补0但chunk长度不含补齐。

格式依据：https://learn.microsoft.com/en-us/windows/win32/directshow/avi-riff-file-reference 。只借鉴Furi Record和SceneManager思想，没有移植Flipper框架代码。
