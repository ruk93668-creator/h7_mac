# Application V2 — RAM / SDRAM / Flash / CPU / 调度预算

## 1. 数字的证据等级

输入源码8a03596。SOURCE_CALCULATED＝由本基线常量/声明算出；DESIGN_ESTIMATE＝尚未批准的布局/容量预算；UNMEASURED＝本轮没有ARMCC5/Map/时序测量；NOT_RUN＝未执行测试/编译。源码计数和计算不是链接后RW/ZI，也不是运行时最坏时延。

本轮只有文档变更，因此没有生产资源配置变化；不声称生成了新的固件或测得增量0。既有产物不作为本轮重新构建结果。

## 2. 当前队列、对象与内部RAM

| 项目 | 基线容量/计算 | 来源及边界 |
|---|---|---|
| 普通消息队列 | 5通道×4×52B＝1040B载荷 | app_messages.c；52B依赖当前ARMCC5短枚举工程ABI/历史布局证据，非host通用布局 |
| Lifecycle队列 | 3通道×4×56B＝672B载荷 | 同上；56B有协议头布局断言；不含StaticQueue/handle/padding |
| Storage命令环 | 4×uint32＝16B，加head/count | app_storage.c；不是FreeRTOS Queue对象 |
| Storage帧环/slot | SLOT_COUNT=1；queue item为3个uint32＝12B | frame/lease/meta及slot结构需目标sizeof；只保存描述符不复制整帧 |
| Qualification | 824B契约尺寸，内含2×Rx332+2×Entry72+16 | app_camera_qualification.h；嵌入App_Camera，不能另加824重复计 |
| Tx/Rx | 144B/332B契约尺寸 | app_camera_lifecycle_replay.h；pending/replay已含消息副本，不再单独加同一消息 |
| SDMMC AXI区 | 32768B，32B对齐，0x24000000起 | bsp_sdmmc.h、bsp_sdmmc_memory.c::.sdmmc_dma；独占窗口 |
| StorageFS写入中转 | 8192B，32B对齐 | storage_fs.c::s_write_input；与SDMMC区不同 |
| Snapshot回读 | 512B，32B对齐 | app_storage.c::s_readback |
| USB payload | 每个UsbUvcPayload对象packet成员1024B；当前Class有一个s_uvc_payload | usb_uvc_payload.h、usb_uvc_class.c；还有FrameView、token、控制/端点状态，不能把1024当整个对象 |
| StorageFS文件对象 | 固定4槽 | storage_fs.c；FIL大小依赖FatFS配置，未测 |
| Framework Event/Timer | Event订阅16、队列16、每次最多分派4；Timer池16 | framework_event.c、framework_timer.c；本轮不实例化更多对象或增加Task |
| 跨核共享窗口 | 4096B，0x38000000起 | Common/Inc/ipc_config.h；不是CM7普通私有heap |

已知载荷不能覆盖所有RAM：TCB、队列控制块、句柄、应用上下文、FIFO元数据、HAL对象、USB/FatFS结构、IPC与其他Framework对象需按真实Map分类。不能把这些表合计为“全部RAM使用量”或把声明地址保留区与链接ZI重复计数。

## 3. 当前任务栈和调度

| 任务角色 | priority | 栈words / bytes（按4B word） | 等待/周期 |
|---|---:|---:|---|
| Camera | 3 | 1536 / 6144 | BOOT前无限，后20ms |
| USB | 4 | 2048 / 8192 | BOOT前无限，后20ms；进度通知可提前唤醒 |
| Storage | 1 | 2048 / 8192 | BOOT前无限，后100ms；命令/帧通知唤醒 |
| Control | 2 | 768 / 3072 | 100ms；5000ms命令期限 |
| IPC | 1 | 512 / 2048 | BOOT后100ms心跳周期 |

合计27648B静态task栈，不含TCB、idle/timer内核任务、MSP/中断栈或CM4栈。配置来自Application/Tasks各C和ipc_config.h。本轮不调整。V2候选仍一一映射这五个角色，不另建Graph、Resource、Lifecycle或Supervisor任务；如预算超限先报告，不擅增栈。

后续逐模块记录ARMCC5调用图、最大自动变量、间接调用目标和真实high-water；仅凭调用层数不能证明最坏栈。迭代图验证的workspace应由初始化装配处静态提供，避免把大数组放Camera/USB高优先级栈。

## 4. 当前SDRAM地址与缓冲容量

| 区域 | 半开地址范围/容量 | 说明 |
|---|---|---|
| 软件允许SDRAM窗口 | [0xD0000000,0xD0800000)，8MiB | bsp_sdram.h；不是对全部区域空闲的保证 |
| 固定帧池 | [0xD0000400,0xD041A400)，7×614400＝4300800B | camera_capture.c；DMA target与IN_USE隔离 |
| JPEG FIFO | [0xD0480000,0xD0500000)，524288B | camera_mjpeg_fifo.h；64块×8192 |
| JPEG drop区 | [0xD0500000,0xD0504000)，16384B | 两个8KiB安全接收目标，不是额外可借帧 |
| AVI索引区 | [0xD0590000,0xD05A0000)，65536B | mjpeg_avi.h；4096×16索引项 |

上述三个独立数据用途（固定池、JPEG含drop、AVI索引）源码保留范围合计4907008B。虽然raw/JPEG采集模式互斥，地址规划仍不能重叠或擅自改用同一空间。窗口内其余间隙是否能用，需同时审查所有绝对地址、MPU、scatter和其他模块；不直接宣称剩余为可分配heap。

Snapshot非JPEG使用63KiB容量，JPEG单次上限262140B，起点同CAMERA_BUFFER_BASE，属于固定区模式复用，不另加到SDRAM总量。JPEG帧队列7、Loan最多7，表示描述/在途上限，不表示七个额外70000B帧数组。Storage帧限70000B，原池slot1，不增加私有整帧复制。

DMA地址32B对齐与JPEG帧起始地址必须分开：JPEG SOI可能在块内任意字节位置，两span视图不能被要求每段都32B起始。需要DMA读的端口自行满足实际对齐/可达性，不能为统一接口偷偷复制或拒绝原有效帧。

## 5. V2增量预算方案（全部UNRESOLVED）

| 分类 | 暂定设计预算方法/估算 | 审核条件 |
|---|---|---|
| Graph ROM | R2第二轮：10Entity×20+14Pad×16+10Link×16+10Caps×48+7Pipeline×108+3Format×56+4FrameRate×8+5Ops×8+GraphDesc40+CheckContext8＝2108B；17个名称各≤32B，共≤2652B产品描述数据 | 替代Phase0的2528B占位估算；均按指针4B/普通ARM32对齐预测，需ARMCC5/Map；不含代码/协议规则表/请求表/链接填充 |
| Graph RAM | R2第二轮 Scratch280+Issue28+已验证视图168＝476B；可变请求每条124B，最多8条992B，同时保留总1468B | 替代Phase0的232B占位估算；const请求改计ROM（124×N）；初期BOOT前验证/安装，运行只读；无Graph Task或可变注册表 |
| 资格/事务 | 优先复用/迁移现824B核心和既有Tx/Rx；4方向各1Tx+1Rx理论1904B | 不在本轮实例化；是否需要全部方向待审；避免与824内两Rx重复计 |
| Grant/Handoff关联 | 初期评估USB1、Storage1、Camera内部1三个并发交接记录；若每条64～128B，则192～384B | 容量必须来自真实可达交接，不是另加引用计数；字段和旧消息存活边界待精确审核 |
| Terminal Receipt | 每条暂估64～96B；容量至少覆盖未消费的合法终结消息 | 精确条数UNRESOLVED；满时有界背压，不覆盖旧身份，不无限增大，不阻塞无关SD-only |
| Supervisor计划 | 固定计划记录+当前步骤身份+完成记录；按实际最大并行计划P×sizeof(plan) | P和取消策略未批准，不给虚假精确总数 |
| 模块/Diagnostics | 新模块替换旧上下文，不在发布固件同时保留新旧对象；诊断按来源/时间投影 | 目标sizeof和Map逐项净增减，不把双应用实验镜像当最终成本 |
| SDRAM数据 | 推荐复用后端，不新增整帧池，初期设计目标增量0 | 尚未实施，实际由地址审计/Map/板上验证；不是本轮测量值 |
| 软件Flash | Graph/Format/Resource/Lifecycle/Supervisor的代码+const表，扣除删除Legacy部分 | 未编码故UNMEASURED；不编造字节数或声称一定减少 |

不能把多项UNRESOLVED估算相加，宣称最终RAM预算已批准；模型容量冻结后要出每bank和每阶段真实预算。若现有RAM无法满足，先缩小静态可用功能或提请设计审核，不增加动态分配/任务栈绕过。

## 6. CPU、实时性和临界区

Graph全量验证初期仅在BOOT前静态配置时做，未来运行期重配必须先证明资源排空并另审发布同步。R2上限Entity16/Pad32/Link32/Pipeline8；稠密ID Getter直接索引且先检查范围；Catalog重复检查O(N²+P²+L²)，每路由可达/环O(N×L)，K路最坏O(K×N×L)+K²依赖检查，合并视图最多N+S次纯Ops（S≤5，即最多21次）和有界GCD；禁止递归。WCET微秒数UNMEASURED，后续DWT测量覆盖最大表、失败分支及中断干扰。不是在DMA ISR或每帧做全图扫描。

现有Camera Bridge每轮最多处理4条消息，Camera控制至多1条，Storage至多1命令/1帧；V2应明确每轮budget，不能因额外Lifecycle收发饿死Return。PRIMASK保护只做有界元数据提交，文件/USB Abort/SD等待在外。Framework Event/Timer不是默认可多任务并发的可靠运输，不增加高优先级周期轮询。

按用户确认稳定输出基线raw160×120×2×10计算有效载荷384000B/s；当前源码默认25对应960000B/s目标流量（SOURCE_CALCULATED，不是实际FPS）。10FPS基线未附本次固件SHA/测量窗口，不冒充本轮独立实测。JPEG40000B×25上限规划约1000000B/s/消费者；双sink和CPU/IDMA中转会增加内存访问，不能将输出字节数视作总线负载。SDMMC当前8192B复制及1ms间隔保留，FE因果关系不由带宽算式证明。CRC/解析/写入/同步和高优先级USB竞争需实板分项测量。

## 7. 扩展影响

| 扩展 | RAM/SDRAM/Flash影响 | CPU/实时性与所有权约束 |
|---|---|---|
| USB | 原端点/packet/token保留，无新帧池；多Class另审 | Cancel/异步完成和ACK消费保持，不因Mode Stop伪造归还 |
| Storage | slot1与AVI索引/中转不变；增槽/增速不在本轮 | 文件失败与资源安全分开；写卡不阻塞Camera owner消息循环 |
| Ethernet | 驱动描述符、网络栈和发送缓存均UNRESOLVED，不预分配 | 多DMA读原帧必须全部完成；独立背压/丢帧，不能占住其他消费者资源 |
| Wi-Fi | 驱动/模块固件接口、网络缓存/复制成本UNRESOLVED | 接受数据与发送/归还不同；断线不强退引用 |
| AI | 推理模型Flash/RAM、tensor及可选转换缓冲另预算 | 只有真实持有Frame才成为consumer；推理取消与DMA读取结束需证明；不能静默改变采集格式 |

Network/AI Task数量和优先级UNRESOLVED，不能以未来支持为由现在增Task。当前USB+Storage双消费者条件不自动证明三路或四路安全；后端能力和容量先审后扩。

## 8. 未来预算验收记录要求

源码SHA、板型、ARMCC版本/短枚举、有效Keil源表、CM7/CM4 Map、scatter与绝对地址清单、每bank RW/ZI、Flash RO代码/数据、全部结构偏移、静态任务配置、调用图和high-water、队列/slot/receipt最大水位、DWT时延与IRQ占用、固件哈希。当前这些新增实测统一NOT_RUN/UNMEASURED。本文件提供可执行预算模型，不冒充验收结果。

### R2布局预算说明

具体声明见ModuleContract第1.2、1.3、1.7节。第二轮预测尺寸：Entity20、Pad16、Link16、Pipeline108、Caps48、Format56、FrameRate8、SinkPolicy12、GraphDesc40、Ops8、CheckContext8、Issue28、Selection124、Workspace112、ValidatedView168、Scratch280B。采用固定宽度枚举码和包装ID，不改旧消息ABI；未编译，全部仍为DESIGN_ESTIMATE/UNRESOLVED。当前主表10 Entity/14 Pad/10 Link/7 Pipeline，留6/18/22/1余量，不为所有未来sink组合预创建Pipeline，不默默扩容。

与首轮cd8daa6的2168/2744B描述和940B RAM预测相比，第二轮主表归并Capture并分离Sink速率，布局同时变化，不能把差额当作已测优化。按**同一第二轮schema**比较A/B实体方案时，B比A多116B描述、名称最多32B；软件适配Flash、纯Storage查询局部16B及uint64算术临时栈均需后续ARMCC5调用图/Map实测。保持所有现有任务栈配置，不因估算擅自扩大。

三种证据分开：FORMAT_VALIDATED、DEVICE_CONFIG_APPLIED、RUNTIME_MEASURED，详细门禁见ModuleContract第1.15节。Graph预算没有给出采集/传送/保存25FPS保证，也没有解决两个Resource NOT_PROVEN。

### R3资源关联预算审核

输入54718ee，仅设计。CoreMediaFormat从原Graph文档类型移为Core中立定义、GraphFormat作别名，预测仍56B，不增加第二份媒体值；R2描述数据2108B/含名称2652B、最坏请求工作区1468B估算保持，未重新编译测量。Source采集、USB交付、Storage采样分别预算，不将record_fps或UVC目标25当Source实测。

R3新增成本项目为两端单写D发布/绑定/消费/退休字段、原Storage槽内G/H关联、独立T、只读查询workspace及可能的票据/捕获实例代次。既有Tx/Rx、824B资格核心、后端Loan/refcount不重复计。精确容量和字段仍待批准，不能把Phase0每记录64～128B或T64～96B范围当确定布局/安全容量；Flash代码、每bank RAM、O0/O2间接调用栈和同步时延均UNMEASURED。Media数据池继续复用，不提出新Frame Pool、任务、队列或动态内存。

后续必须按真实并发交接和未消费T峰值推导容量，检查Storage/USB单路背压不会死锁另一方；静态查询workspace不塞入高优先级栈。结构冻结后才运行ARMCC5 sizeof/offsetof、Map与high-water审核。本轮所有生产字节未改，不能把“源码无改”写成新固件资源测量通过。两个NOT_PROVEN不变。
