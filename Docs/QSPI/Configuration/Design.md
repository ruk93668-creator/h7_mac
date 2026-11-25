# Persistent Recording Configuration 产品设计（待审核）

日期：2026-11-25。冻结 main：`62b42d7c0b7df9eadafa00953ee8ce1229da9826`；冻结 QSPI：`3a5c942dfd1420788102b11cc5271b188f319bf2`。本提交只有设计文档；不是 Q1.5-A 验收、产品写授权或实板许可。Q1.5-A 仍按独立任务实施，不混入本设计生产差异。

本文的“事实”来自这两个提交的源码；“方案”均未实施；供电、掉电完整性、任务前启动调用及产品保存性能均为 NOT_VERIFIED。存储格式见 [双 Slot 协议](StorageProtocol.md)，时序/API/白名单见 [集成方案](Integration.md)，分阶段计划、寿命和验收见 [验证计划](Validation.md)。

15份关键源码已逐字节对照冻结Git对象，其SHA-256与本轮核对范围见 [源码证据](SourceEvidence.md)。

## 真实配置依赖（已核验事实）

main 源码引用统一指 [冻结 main 树](https://github.com/ruk93668-creator/h7_mac/tree/62b42d7c0b7df9eadafa00953ee8ce1229da9826)，不能以当前 QSPI 工作树中的旧 Application 替代。

|源码证据|实际行为|设计影响|
|---|---|---|
|`CM7/ApplicationV3/Core/video_config.h:7–24`、`video_config.c:12–82`|VideoConfig 与 AppStorageConfig 为静态 const；录像默认 partition=3（可编译覆盖0..4）、fps=25、容量256MiB、时长0|持久化独立四字段；出厂默认取产品编译默认，不把 Flash 字节强转 AppStorageConfig|
|`CM7/ApplicationV3/Core/app_context.h:7–14`、`app_main.c:12–27`|App_Init 在 APPLICATION 级绑定两份 const 指针；服务只读上下文|增加初始化适配器，完成后发布终身稳定快照；不替换运行中指针、不改服务私有状态|
|`CM7/ApplicationV3/Services/Storage/storage_service.c:139`、`:598–619`|挂载使用 partition；Init 检查 fps 1..60，保留配置指针|1..60只是现接口检查，不能宣传全部帧率已验证；挂载失败不能自动改选其他分区|
|同文件`:241–261`、`:315`、`:529`|fps用于取帧间隔 `1000/fps` 和初始AVI参数；容量/时长使本次录制转 draining|这些参数不是 Camera 传感器帧率或严格定时器；当前没有自动连续分段滚动策略|
|`CM7/Service/mjpeg_avi.c:28`、`:62–71`、`:108–115`、`mjpeg_avi.h:7–11`|允许fps到60，帧4..70000B、最多65536索引、Finish要求position<=256MiB；终结按实际时间修正平均帧率|产品更保守限制容量与fps；时间/容量为上限触发，仍可能先遇到索引、错误或主动停止|
|`CM7/Platform/storage_fs.c:86–138`、`:705–746`|partition=0自动检查，1..4选MBR分区，依当前FatFS/检查逻辑挂载|保存分区号不代表分区存在、可写或有空间；不新增GPT/格式化能力|
|`CM7/Core/Src/main.c:173–194`|App_TasksCreate 在 Framework_InitRuntime 之前；之后才启动调度|单加 SERVICE Initcall 无法满足“服务任务创建之前加载”，必须另行审核显式前置准备点|

`total_bytes`、`reserve_bytes`、`max_segments` 当前仅声明/默认初始化，Storage Service 不消费它们；StorageFS_GetSpace 的局部 total_bytes 是文件系统统计，并非该配置字段。第一版不序列化这三项、不定义保留空间/循环覆盖/累计容量语义；适配时从编译默认复制原值。未来需要真实策略时另升 Schema。

当前固定视频模式：JPEG 320×240、输出目标25fps/40000B；Raw YUV422 160×120、输出目标10fps（`video_config.c:12–33`）。录像 Append 只接受 JPEG。输出目标不等于传感器稳定能力或SD持续吞吐保证。第一版不改变模式、分辨率、像素格式、帧缓冲、USB描述符或输出目标。Raw 产品可加载/保存四字段以备下次 JPEG 产品使用，但录像功能标为 `UNAVAILABLE_FORMAT`，不把 Raw 转成 MJPEG，也不修改原服务行为。

## 四字段数据模型（设计建议）

业务模型建议 `RecordingConfig_t`：四个 uint32_t 字段，顺序为 partition、record_fps、segment_bytes、segment_ms。线格式16B，显式小端编码；内存布局不作为Flash ABI。与 AppStorageConfig 分离，Configuration Service 不依赖 ApplicationV3 类型。

|字段|Schema 1合法值|业务约束和验证|
|---|---|---|
|partition|0..4|0沿用自动选择；1..4精确MBR号。保存不访问/格式化SD；实际挂载按原服务执行，失败保留诊断，不能自动切分区造成写错目标|
|record_fps|1..25（整数）|25是首版产品策略上限，不是HAL极限；不增大当前JPEG目标。低帧率是抽样目标；实测受相机帧供给、整数毫秒、服务调度/SD影响。不得承诺精确fps|
|segment_bytes|1MiB..256MiB（含端点），字节|上限与当前AVI Finish一致；无需强制4KiB倍数。包含AVI头、chunk填充及最终索引预算，不是纯JPEG字节。最小值足以容纳当前70000B单帧及索引，仍需Mock验证|
|segment_ms|0，或1000..min(86400000, 65535×floor(1000/record_fps))|0仅关闭时间限制，容量/索引仍有效。乘法以uint64校验；上限约束为保守策略，非调度精度保证。时间差采用uint32无符号差，范围小于2^31毫秒|

字段在解析、Stage与Commit三处执行同一验证器。整份拒绝，不把损坏字段“修剪”为合法值；非法持久副本不拼接另一个副本的字段。配置一致性仅限录像参数，不证明SD空闲容量。容量或时长到达时沿用当前“停止并完成本次AVI”，第一版不实现下一段自动创建、文件保留、删除或循环覆盖。

出厂配置由 `VideoConfig_GetStorage()` 的产品默认值映射；典型值 `{3,25,268435456,0}`。编译覆盖partition须同样验证。若产品编译默认也非法，初始化失败且不创建服务任务，不能发布空/半初始化上下文。版本升级后新默认由固件决定，不能从任意未验证Flash内容推导。

## 分层、所有权和失效策略

- BSP：保留HAL句柄、原子事务权、页拆分、WEL/保护/BUSY、校验、首错与FAULT/Recover契约；不负责CRC、Schema或产品默认。
- Platform：固定分区读/写适配及一次逻辑事务预约，不暴露HAL/任意地址解锁。预约保护“扫描→选择→擦写→提交”跨多个BSP调用；它不是第二套控制器状态机，不重复BSP硬件锁/状态判断。无Mutex/新任务，短临界区仅取放标志，争用立即BUSY，不持PRIMASK等待HAL。所有产品配置访问必须经此入口，测试裸BSP访问与其互斥。
- Configuration Service：解析、选择、副本提交、默认策略、不可变active快照、可修改staged副本、诊断。只同步API，不新增Configuration Task、事件总线、消息池、Supervisor成员或产品任务。
- ApplicationV3适配：把已初始化的四字段覆盖到编译默认的AppStorageConfig副本，其余三字段保持默认；绑定const上下文。active与staged/persisted分离，直到下次重启才改变服务看到的值。

ConfigCommit 只在已批准的维护上下文执行。第一版明确禁止在采集、UVC、SD录制活跃时同步保存；现有Control所有权和合法产品控制入口先审核，不通过新增测试邮箱或冒充Control解决。没有已证明的静止条件时返回INVALID_STATE。停止服务不等于允许任意调用者擦写。读/保存均由M7独占，CM4不访问QUADSPI或配置RAM，不改80B IPC。

读坏配置可使用经校验的默认配置继续既有业务，并显式记录 `DEFAULT_EMPTY`、`DEFAULT_CORRUPT`、`DEFAULT_UNSUPPORTED` 或 `DEFAULT_IO_ERROR`。默认并非静默成功，Configuration诊断同时保留底层原错。硬件/读取错误禁用本次保存，不自动Init/Recover重试；BSP FAULT和写入完成未知不被默认机制清除。

两个Slot都不兼容、同序号冲突、半圈歧义、读取不完整时，保存一律关闭。出现未知新版本时允许默认启动旧业务（需用户审核此策略），但不自动覆写未知数据或回退为较旧副本并继续保存。默认值恢复是显式提交新一代默认记录，不是擦除双Slot。

## 当前阻塞和批准边界

1. 两个产品扇区未分配。不得复用H1末尾测试扇区或相邻证据扇区；下面仅使用符号地址，提交设计不产生写许可。
2. BSP公共策略 `{0,0}` 会拒绝全部产品Program/Erase。Q3必须单独批准编译期固定两个Slot白名单，无公共Setter、不复用H1宏、不放开整片。
3. EraseSector现契约要求调用者持久化保存完整旧扇区备份。静态RAM副本满足逐字节预检，**不满足持久化要求**；上一份有效配置只是业务恢复点，不等价于待擦扇区完整备份。Q3前必须选定外部持久备份方案，或单独批准“已淘汰副本可丢弃”的明确产品契约修订，不能由Platform自行豁免。
4. 任务创建前的TIM6活性及main栈、启动延时尚未验证。失败时不在屏蔽TIM6环境调用HAL；不能靠停止Tick缩短预算。
5. 双副本方案依赖非目标扇区不被掉电影响、Flash合规供电和检测。CRC不是认证或绝对防误判机制，不能宣称任意欠压均安全。
6. Q1.5-A共存结果尚不由本设计给出；产品集成和保存阶段必须以其独立审查结果为前提。当前板上最后固件为H2仅沿用用户确认，本轮未连接实板。

以上均为待审核事项，不自行选择地址、改变备份契约、开放写入或执行下载。
