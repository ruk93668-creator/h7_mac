# MSC-0 当前源码审计总览（2026-11-16）

> 本节替代下方历史“目录为空”的当前状态结论；历史原文完整保留，不能再用作当前审计证据。
> VERIFIED：基线 f0ce45fd9ab7250b1e22260e087d606ee5ed9ecb，codex/usb-msc-bringup，origin=https://github.com/ruk93668-creator/h7_mac.git。开始时只有原有docs/msc-0未跟踪，生产树无修改。

## 状态与交付索引

VERIFIED=当前源码确认；INFERRED=推断/拟议设计；UNRESOLVED=待确认；NOT_TESTED=未实测。设计未实现，无硬件符合性声明。

1. [USB Device全量路径/结构/回调与复用](USB_Device_Audit.md)
2. [SDMMC/FatFS及DMA/Cache/Buffer安全](SDMMC_FatFS_Audit.md)
3. [MSC分层、BOT、SCSI与LUN](MSC_Protocol_Architecture.md)
4. [FAT卷所有权与异常锁定](MSC_MediaOwnership_Design.md)
5. [UVC+MSC复合可行性与预算](UVC_MSC_Composite_Analysis.md)
6. [MSC-1～MSC-7文件/接口/测试/验收/依赖/回滚](MSC_Implementation_Plan.md)

## 核心结论

- VERIFIED：当前有效USB构建单Class；Core有条件composite分支但builder缺失；Platform单Class上下文/1024字节唯一pool。不能直接重复RegisterClass获得复合设备。
- INFERRED：独立MSC可复用现有USB Core/LL和Bulk端点API；BOT/SCSI放Service，SD adapter放Platform，所有权由Application协调。
- VERIFIED：USB DMA关闭，PIO逐包IN；31字节CBW需要64字节OUT接收槽；提交不等于完成。
- VERIFIED：SD_Block独立API，FATFS/RAW任务租约，512字节扇区、同步读写；IDMA固定32KiB非缓存AXI staging。8KiB是输入/复制分段，IDMA最大批次32KiB。
- VERIFIED：FatFS不重入，StorageTask当前独占访问；Unmount不自动flush，需Sync/Close且无活动文件。
- INFERRED：最小独立MSC方案可保持Core主体不改，但异步BOT Reset的EP0延后状态必须专门设计。UVC+MSC还需路由/接口上限/上下文和独立生命周期处理。
- INFERRED：按当前BSP公式复合FIFO752/1024words；不代表实机资源或并发带宽通过。现有UVC满包FS校验没有给MSC保留吞吐。
- INFERRED：PC拥有卷时禁止FatFS；断线/Reset/Suspend不证明安全归还，未知归还进入UNCERTAIN_LOCKED。现有租约不能替代卷所有权协议。
- UNRESOLVED：芯片资源实板复核、物理写保护、异步Reset握手、跨重启dirty状态、Windows安全移除命令证据。
- NOT_TESTED：未编译/烧录/跑板/抓包，本阶段仅源码与文档/Git检查。

## Git Diff交付约束

仅这七份指定文档进入提交；初始草案未跟踪内容完整保留于下方。无生产代码变更。提交后以Git tree范围与远端同SHA核对，不把提交SHA写进自身正文造成循环变更。VERIFIED：文档UTF-8、内部链接、四类状态标记及Markdown代码围栏检查通过；git diff --cached --check通过，暂存范围仅指定七份文档、624行新增，工作树无未暂存差异。仓库既有目录规范大小写为`Docs`，Windows上的`docs/msc-0`与其为同一目录，未建立第二套文档目录。最终回复报告实际SHA和推送结果。MSC-0到此停止，等待人工审核，不进入MSC-1。

---

# 历史受阻草案（原文保留，不代表当前源码状态）

# MSC-0 审计受阻报告与待验证设计草案

日期：2026-11-16（Asia/Shanghai）
工作目录：D:\32\STM32H7\h7_mac_sd
状态：源码审计受阻；本文设计仅为候选方案，不能作为源码审计通过或实现批准。

## 1. Git 与输入证据

只读检查结果：
- Get-Location 返回指定工作目录。
- Get-ChildItem -Force 无输出，目录为空。
- 递归查找 AGENTS.md 无输出。
- git status --short、git remote -v、git branch --show-current、git rev-parse HEAD、git log -5 --oneline 均报告 not a git repository。
- rg --files 源文件过滤无结果。

仓库、远端、分支、HEAD、基线和历史均不可确认。未初始化仓库、未建立分支、未切换/重置基线。未访问或修改 h7_mac、h7_mac_wk。目录为空不能替代有效 Git 工作区的 clean 结论。

恢复前置条件：将正确仓库及所需基线提供到本目录，或明确指定可用源码位置和授权基线。收到源码后重新检查未提交修改；保留所有用户修改，再决定是否从当前已确认基线建立 codex/usb-msc-bringup。

## 2. USB Device 源码审计报告：未能执行

没有任何源码，不能给出真实文件路径、结构体、函数、调用关系或复用结论。下列所有项目均为未知，不可据此声称现有 Core 支持复合设备。

|审计项|恢复源码后必须记录的证据|
|---|---|
|Core / Class 注册|设备句柄、Class 表/单指针、注册调用、Init/DeInit 顺序和接口/端点路由|
|描述符|设备/配置/字符串/速度描述符，长度计算与 Interface/EP 编号|
|EP0 SETUP|标准/类/厂商请求入口、recipient 与 wIndex 校验、EP0 Data/Status 回调归属|
|EP Open/Close|底层驱动调用、端点类型/包长、打开失败处理和关闭完成条件|
|Bulk IN/OUT|提交入口、长度限制、短包/ZLP、忙返回、回调和取消|
|中断/完成|IRQ→HAL/LL→Core→Class 调用链，ISR/Task 边界与 RTOS API 合法性|
|启动/停止/状态|Default/Addressed/Configured/Suspended 等状态及 VBUS/reset/disconnect 行为|
|UVC|控制/流接口、alternate setting、ISO 提交、缓冲区回收、启动停止依赖|
|FIFO|RX/TX FIFO 配置、硬件实例/速度/PHY、每个端点实际资源和总预算|

逐个文件形成证据表：路径与行号、结构体、函数、调用者/被调者、Task/ISR、缓冲区所有者、完成/取消条件、复用判断。必须读取实际实现及所有调用点，不能以文件名推断。

若 Core 仅保存单个 Class 指针，候选最小范围是增加复合 Class 分发器，集中生成配置描述符，按 Interface/Endpoint 路由 Setup/DataIn/DataOut，跟踪 EP0 当前事务归属，并分发 Reset/Init/DeInit/Suspend/Resume。若底层 Core 固定单 Class 或共享私有数据，仍可能需要修改注册/上下文存储；是否可完全通过外层分发器实现必须看源码。本阶段不修改。

## 3. SDMMC / FatFS 源码审计报告：未能执行

|核对项|当前结论|必要源码证据|
|---|---|---|
|初始化、IDMA、读写、sync、capacity|未知|初始化时钟/总线宽度、传输 API、卡状态轮询、错误/超时路径|
|512 字节逻辑扇区|未确认|卡信息、diskio ioctl、块地址换算与 FatFS 配置一致性|
|32 KiB 非缓存 AXI SRAM 缓冲区|未确认|定义、链接 map/段、地址、MPU 属性、实际分配长度|
|8 KiB 分批|未确认|调用点、分批循环和 DMA 最大计数|
|AXI SRAM / DTCM 可达性|未知|芯片总线、USB/SDMMC DMA 可达地址与链接布局|
|对齐与 DCache|未知|缓冲区地址/大小、cache line、clean/invalidate 范围和屏障|
|diskio / StorageFS|未知|读写/sync/ioctl、错误映射、调用上下文|
|mount/unmount、文件句柄|未知|全部 f_mount/f_open/f_close/f_sync 及包装层调用者|
|StorageTask 独占|未确认|任务入口、队列、直接访问旁路、中断访问|
|FatFS 重入|未确认|ffconf.h、锁实现和版本；重入配置不代表允许 PC/MCU 共享卷|
|恢复|未知|拔卡/超时/CRC/IDMA abort/reinit 以及残留请求清理|

历史参数不得作为本工程事实。Block Sync 必须说明是否仅队列排空、是否等待 DMA 完成及卡回到可接受命令状态；不能只以 DMA 中断代表介质操作完成。

## 4. MSC BOT / SCSI 候选设计

以下为协议设计草案，尚未对照 USB-IF BOT、MSC/SCSI 规范原文核验；实施前必须完成规范审查及 BOT 错误矩阵测试。

### 描述符与 LUN

单 LUN（LUN 0），MSC interface bInterfaceClass=0x08、bInterfaceSubClass=0x06、bInterfaceProtocol=0x50。一个 Bulk OUT、一个 Bulk IN；具体接口号和 EP 地址待真实 UVC 分配后确定。Full-Speed Bulk 最大包长 64；仅在硬件/PHY/栈确实支持 High-Speed 时提供 512 字节 HS Bulk 及匹配的速度描述符，不能由 H747 型号推断当前连接速度。

GET_MAX_LUN 返回 0；校验请求方向、recipient、wValue/wIndex/wLength。Mass Storage Reset 使用正确 interface，执行协议复位并协调在途 IO；Reset 不改变 FAT 卷所有权。仅向 PC 暴露已经明确转移的介质。

维护 present、ready、write_protected、block_count、block_size、media_generation、ejected/prevent、sense、unit_attention。物理卡存在与允许访问分别建模。容量采用足够宽整数，READ CAPACITY(10) 返回最后一个 LBA 与块长；超出其表达范围须明确采用扩展命令策略或不支持的容量上限，不能截断。插拔触发 generation 变化和 Unit Attention，旧请求不得作用于新插入的卡。

### BOT 包与状态

CBW 固定 31 字节：signature=0x43425355，tag、host transfer length、方向、LUN、CDB length、最多 16 字节 CDB。CSW 固定 13 字节：signature=0x53425355，回显 tag，residue，status 0/1/2。使用显式字节序解析，避免直接解引用未对齐结构体；SCSI CDB 字段为大端，BOT 多字节字段为小端。

候选状态：DISABLED → WAIT_CBW → VALIDATE → DATA_IN / DATA_OUT / NO_DATA → SEND_CSW → WAIT_CBW；另设 HALTED / RESET_RECOVERY / ABORTING。端点完成回调仅确认对应传输完成，通过事件交给 worker 推进 SD 请求；ISR 不执行阻塞 SDMMC 或 FatFS 操作。

区分 invalid CBW、有效但不能执行的 SCSI、host/device 数据方向或长度冲突。Invalid CBW 进入规范要求的 stall/reset recovery，不能伪造成功 CSW。SCSI 失败记录 sense 并返回 failed CSW。Phase error 返回 phase-error CSW 的适用条件和 stall 时序必须按 BOT 13-case 矩阵实现，不能用一个通用错误分支覆盖。

实施前逐项填完整矩阵：Hn/Hi/Ho 与 Dn/Di/Do、长度大于/等于/小于、何时 stall IN/OUT、是否传输/丢弃数据、residue、CSW 状态、何时必须 reset recovery。residue 以 host 期望长度及命令实际处理字节计算并防下溢。未实现该矩阵不可验收 MSC-2。

每次只允许一个 CBW。WRITE 数据流严格限长，检查 LBA+count 溢出和范围，跨块分片仅在完整扇区形成后提交。READ 缓冲区在 USB 完成之前不可复用。零块命令、短包、ZLP、超长/截断 CBW 和重复回调均须测试。CSW 只有在其 IN 完成后才重新接收下一条 CBW。

Reset Recovery 必须协调 MSC Reset、端点 halt 清除和硬件在途请求取消；顺序与端点数据 toggle 恢复需按规范/栈验证。禁止在旧 DMA 未释放缓冲区时重新 arm CBW。USB bus reset 只是传输终止事件，不是主机安全弹出或卷归还证明。

### SCSI 命令策略

|命令|计划|行为|
|---|---|---|
|INQUIRY|必须|标准设备信息；合法处理 allocation length；未支持 VPD 用合法 sense 拒绝|
|TEST UNIT READY|必须|按所有权/介质/初始化状态返回 ready 或 NOT READY/UNIT ATTENTION|
|REQUEST SENSE|必须|固定格式 sense；按规范处理并清除/保留状态，限制返回长度|
|READ CAPACITY(10)|必须|ready 后返回最后 LBA 和逻辑扇区大小|
|READ(10)|必须|检查 CDB、范围、长度、generation；分批读块|
|WRITE(10)|必须|同上，另查写保护；完成标准需包含底层写入完成|
|MODE SENSE(6)|必须设计并实现基础响应|报告写保护和已支持页；不支持页返回 INVALID FIELD IN CDB|
|START STOP UNIT|所有权流程必需|LoEj/Start 合法组合触发介质卸载/装载策略；同步完成后才确认 eject|
|PREVENT ALLOW MEDIUM REMOVAL|必须|记录防移除状态，影响逻辑 eject；物理拔出仍按故障处理|
|SYNCHRONIZE CACHE(10)|必须|排空设备写入并等待卡完成；不支持字段返回合法错误|
|其余命令|逐项扩展|不支持 opcode 返回 ILLEGAL REQUEST/INVALID COMMAND OPERATION CODE，绝不统一成功|

sense 至少覆盖介质不存在、尚未就绪、介质变化、写保护、LBA 越界、非法 opcode/CDB 字段、读写介质错误、硬件错误。具体 ASC/ASCQ 和 Unit Attention 优先级在规范核验后固定。SYNCHRONIZE CACHE 成功不证明主机未来不会再写，不能单独归还所有权。Windows 枚举/读写兼容性必须由真实命令捕获验证。

## 5. FAT 卷所有权候选状态机（仅设计）

状态：MCU_OWNED、QUIESCING、PC_OWNED、RETURNING、UNCERTAIN_LOCKED、NO_MEDIA。单一协调者串行处理事件；所有读写入口强制校验 owner 和 generation，不能仅在 UI 中禁用。

MCU_OWNED → QUIESCING：拒绝新的本地文件请求，等待在途本地 IO，所有文件 sync、close，底层 sync，卸载 FatFS。每步成功且无残留引用/DMA 才进入 PC_OWNED 并向 MSC 报 ready。任一步失败不暴露介质；能证明恢复完整时退回 MCU_OWNED，否则锁定。FatFS 卸载 API 的真实返回含义及失败路径待源码核对。

PC_OWNED：FatFS 禁止 mount/读写及后台日志/诊断旁路。MCU 请求使用 SD 时仅记录请求，不抢占；等待 PC 执行可验证的弹出协议。Suspend、Reset、重新枚举不解除所有权。

PC_OWNED → RETURNING：收到符合策略的 START STOP eject，先拒绝新媒体命令，等待已接受的 OUT、块写和卡忙结束，sync，按协议完成命令/CSW 并锁住 LUN。确认传输完成、没有 DMA 或队列持有缓存、介质 generation 一致后，协调者才允许重新挂载进入 MCU_OWNED。需要明确定义主机重新 load 与 MCU 挂载的互斥仲裁，不能因后续重新枚举自动向 PC 暴露正在挂载的卷。

异常规则：
- 写入中拔线、无安全弹出、只有 disconnect：进入 UNCERTAIN_LOCKED；设备无法证明 PC 缓存已写回，也无法恢复未到达设备的数据。
- USB reset/suspend：保留 PC 所有权或锁定，取消协议事务但等待 DMA 终止确认；不触发 MCU 自动 mount。
- SD 写失败/超时：报告失败 sense，阻止继续写，锁定；不能假设命令未造成部分写入。
- 安全归还中断、CSW 未确认完成、unmount/sync 失败：不能进入 MCU_OWNED。
- 拔卡：NO_MEDIA，旧 IO 全部失效并完成取消；重新插入按新介质处理，不能继承旧 ready/安全归还标记。
- 重新上电：RAM 状态丢失不能作为安全证明；需设计可校验的持久 dirty/ownership 标记或保守启动策略。持久标记位置/原子性/磨损和掉电窗口必须单独评审，不能随意写 FAT 卷保留扇区。

UNCERTAIN_LOCKED 的退出必须走明确的人工恢复/主机修复并验证流程；普通确认按钮、重新插拔或等待超时都不是卷安全证明。不得声称 MCU 可以自动修复主机未落盘缓存。验收需验证每个 FatFS 入口在锁定状态均被拒绝。

## 6. UVC + MSC 可行性：尚无工程结论

协议上可设计复合设备，当前硬件/源码是否可行未知。需建立现有 UVC control/streaming interfaces、alternate settings、IAD、端点地址/方向、包长/周期及 FIFO 表。保留 UVC 接口语义，再分配 MSC interface 和 bulk EP；是否新增/调整 IAD 及设备级 class 字段依描述符规范和 Windows 实测确定。

EP0 以 recipient/interface/endpoint 路由，拒绝越界 wIndex；数据阶段完成使用事务归属，不能依赖全局“最近 Class”。两个 Class 有独立上下文及缓冲区，Class 单独停止只关闭所属 EP；总线 reset 全局协调取消与重建，MSC ownership 独立保留。

Full-Speed 原始速率 12 Mbit/s 不是应用吞吐。周期性 ISO 占用、token/handshake/帧开销、UVC alternate setting、FIFO/IRQ 延迟会限制 Bulk 剩余带宽；ISO 无重传，Bulk 可重试也可能饥饿。必须按实际流格式、帧率、每帧负载和硬件调度做预算，再测并行峰值/长时间运行。不能用单独枚举成功推断联合传输稳定。必要时限制 UVC 档位或暂停流，具体产品策略待证据决定。

## 7. DMA / Cache / Buffer 安全：候选约束

当前 USB DMA 是否启用、SD IDMA 地址、cache 属性和异步完成链全部未知。

首选易审查方案为独立 USB staging 与 SD staging 缓冲区，通过 CPU copy 和明确状态转换交接；以后再评估零拷贝。若 USB 实际采用 FIFO/CPU 模式，必须据源码记录，不能称为 DMA。

缓冲区状态：FREE → SD_READING → CPU_READY → USB_SENDING → FREE；写方向 FREE → USB_RECEIVING → CPU_READY → SD_WRITING → FREE。同一缓冲区禁止 USB 与 SD 并发持有。双缓冲仅允许不同 buffer 并行，提交失败必须回退所有权。

DMA 可达性依据实际主设备总线和链接地址确认；不得把 DTCM 当通用 DMA SRAM。若缓存：CPU→DMA 读前 clean，DMA→CPU 写后 invalidate；DMA 写之前处理 dirty line，禁止 cache line 与其他可写对象共享。地址和长度按实际 cache line 隔离/对齐，禁止 invalidate 波及邻接脏数据。若非缓存：仍需访问顺序屏障及互斥；非缓存不保证生命周期安全。

USB 完成回调、SD DMA 完成、卡编程完成、队列完成是不同条件。每个请求携带 token/generation，取消需确认硬件停止访问后才能回收；晚到回调不得释放新请求的缓冲区。ISR 只投递合法事件，worker 执行阻塞 IO。所有超时有界，超时后必须 abort 并确认 quiescent，不能直接释放 buffer。

## 8. MSC-1～MSC-7 实施计划（文件名均为建议，尚非真实路径）

每阶段使用独立提交，禁止夹带其他模块重构。回滚必须在设备停止/IO 排空后进行；固件回滚不等于文件系统恢复。未确认拥有安全卷前不自动挂载。

|阶段|候选修改文件/接口|测试与验收|依赖与回滚|
|---|---|---|---|
|MSC-1 枚举|新增 msc_desc、msc_class；注册/描述符接入点待审计；Init/DeInit/Setup|Windows 描述符检查、重复枚举/reset、GET_MAX_LUN/Reset、无介质；正确 Class/EP/速度且 UVC 原独立构建通过|先完成真实源码/规范审计；关闭 MSC 构建开关或回退本阶段提交|
|MSC-2 SCSI+BOT|新增 msc_bot、msc_scsi、RAM 测试 LUN；传输提交/完成/取消接口|CBW/CSW、tag/residue、13-case、短包/错误CDB/reset recovery；RAM LUN 读写数据校验与协议抓包通过|MSC-1；回退协议模块并恢复无介质枚举，不触及 SD|
|MSC-3 Block Adapter|新增 msc_block_adapter；geometry/read/write/sync/cancel/media_generation|mock 故障、越界/溢出、写保护、插拔/晚回调；异步生命周期和超时验证通过|MSC-2及SD审计；恢复 RAM LUN|
|MSC-4 SD 实读写|接入真实块设备入口，真实路径待定；先只读，再用专用可销毁测试卡写|全盘/边界/分批随机校验、卡忙/超时/拔卡、缓存开关对照；真实块数据一致，无 FatFS 并发|MSC-3；安全隔离FatFS为前提；回退 RAM LUN，测试卡离线检查|
|MSC-5 所有权|新增 storage_owner 与本地存储访问门禁，StorageTask/挂载入口路径待定|完整转换矩阵、句柄未关/unmount失败、PC写时MCU请求、非安全断开；全部旁路受控且锁定不挂载|MSC-4且状态机设计获审；禁用SD暴露，保持保守锁定策略|
|MSC-6 异常稳定性|协议/存储恢复、错误计数和测试注入接口|长时间读写、reset/suspend/掉电/拔卡矩阵、资源泄漏、重复恢复；数据校验、无死锁、无越权，测试时长/性能阈值预先约定|MSC-5；回退独立错误修复提交，故障卷仍需修复|
|MSC-7 联合验收|新增 composite 描述符/dispatcher；真实Core/FIFO接入点待定，UVC改动另行审批|各UVC档位+MSC双向持续负载、帧丢失/吞吐/延迟/FIFO预算、独立停止与全局reset；达到预先确定产品指标|MSC-6及UVC证据/带宽预算；回退到独立MSC或原UVC构建|

MSC-4 在 MSC-5 完成前只允许在专用测试构建中完全禁止 MCU FatFS 使用该测试卡；不能先接生产卡再补所有权。

## 9. 文件 Diff 与阶段退出

本次仅新增本报告 docs/msc-0/MSC-0-AUDIT-AND-DESIGN-DRAFT.md，无生产代码、工程、链接、MPU、RTOS、M4 或 Legacy 改动。

无 Git 仓库，不能生成真实 git diff/status 报告或确认 tracked/untracked 状态。文件系统检查只能证明本次文档输出，不能替代 Git 差异审查。没有编译/运行测试：目录无源码，且本阶段禁止提前实施。

交付覆盖八个要求的主题，但源码相关报告均明确受阻，设计均待核验。MSC-0 未完成技术验收，不能进入 MSC-1。此处停止，等待人工审核与正确源码/基线输入。
