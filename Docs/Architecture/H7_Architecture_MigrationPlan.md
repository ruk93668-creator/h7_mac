# H7 Architecture Migration Plan — Phase 0

## 1. 本轮产物与冻结

main基线f0ce45fd9ab7250b1e22260e087d606ee5ed9ecb；独立研究分支codex/architecture-media-graph-lifecycle。本轮只提交Docs/Architecture下五份文件；不编译、不运行测试、不修改生产/测试/消息ABI/Replay/M4延时、不合并main。不把C2.2B或V1适配代码当main，不启动C2.2C。

以下全部是待人工批准的迁移提案，不是后续编码授权。采用“先描述、后薄适配、再切换业务”的顺序，不先造万能AppManager/完整Linux或ROS/GStreamer移植层。

## 2. 迁移步骤与依赖

| 阶段 | 工作/预期修改范围 | 入口条件 | 退出证据与兼容性 |
|---|---|---|---|
| Phase0 当前 | 五份研究文档 | main与分支关系已冻结 | 文档范围、事实/参考/提案清楚；不宣称软件/硬件通过 |
| Phase1 拓扑描述 | Media const节点/端口/link描述及只读诊断；沿用Device Registry和Initcall | 人工选择静态图schema | 格式/尺寸/link验证专项，启停/Frame行为无变化，旧ABI不改 |
| Phase2 资源薄适配 | CameraCapture/FIFO固定与JPEG句柄/只读资源视图 | 现有pool/owner不变量审计完成 | 每种格式、双span、真实DMA完成/归还、错误/重复/旧身份证据；不复制引用数 |
| Phase3 生命周期适配 | App模块转换请求/完成视图，Task保持消息执行；现有协议适配 | Phase2可证明真实资源状态；基线选择与所有权方案获审 | 不引入第二状态权威；START失败回退/STOP排空/ACK/FAULT轨迹分别验 |
| Phase4 业务路径切换 | Control旧启动/停止控制权与消费者独立生命周期的单路径迁移 | 前述契约和异常交错通过并获生产授权 | 不同时保留旧/新硬件控制；USB/SD单独/同时运行和单路退出 |
| Phase5 性能/扩展另审 | 仅有测量后考虑缓存/数据面/Network适配 | 独立安全生命周期已验收 | 按DMA/背压/带宽实测；不在Phase0优化25FPS或FE |

Phase1静态图与Phase2调研可并行设计，但不能分别启用两套资源所有者。Phase3/4必须等待资源可验证和明确的业务决策。每阶段独立提交、Draft审查/人工合并，不自动导入C2.2B未完成设计。若用户后续恢复C2.2B，先选择统一的基线及迁移边界，防止两条架构路线同时编码。

## 3. 内存/栈成本（源码算术，不是Map结果）

| 现有项目 | 源码依据 | 原始预算 |
|---|---|---:|
| 固定帧区 | camera_capture.c 7 * 640*480*2 | 4,300,800B SDRAM，另起始offset1024；不是内部ZI |
| JPEG区 | FIFO 512KiB + 两个8KiB drop块 | 540,672B独立SDRAM范围 |
| StorageFS输入 | storage_fs.c s_write_input | 8192B，内部静态缓冲 |
| SDMMC AXI bounce | bsp_sdmmc_memory.c/.h | 32768B专用AXI；不可与图/Frame池混用 |
| Camera/USB/Storage/Control栈 | 1536/2048/2048/768 words | 按ARM32 word4B共25,600B；不含IPC/TCB/对齐 |
| 普通队列payload | 5通道*4*52 | 1040B，52B为现有ABI约定，本轮未重测sizeof |
| 生命周期payload | 3通道*4*56 | 672B，同上，不含StaticQueue/handles |

Snapshot与固定帧模式存在复用地址，不能把所有宏简单相加当峰值；外部SDRAM窗口不等于链接器RW/ZI。栈深、堆/内部RAM总量、Cache/MPU性能必须后续用实际ARMCC5 Map/HTM及板上水位确定；旧支线Map不能冒充此研究分支新构建。

PROPOSAL预算示例而非定案：若8节点各24B、12端口各12B、8链各12B，静态图约432B RO；如有8份16B转换事务元数据需128B，但优先复用原请求记录，不重复保存业务state。资源句柄若8份各32B约256B，必须附于真实账本或短期描述，不是再建一套pool/refcount。不同方案不能直接累加，结构/对齐需实际sizeof。不得新增每节点一个Task：仅一个额外2Kiword栈就会增加约8KiB，远大于上述元数据。

## 4. 实时性与同步成本

当前USB优先级4、Camera3、Control2、Storage1；Camera/USB BOOT后20ms等待，Storage100ms；本研究不调整。ISR仅发布完成/错误事件，Task再处理；graph静态验证放配置边界，不能在每个DMA完成IRQ动态遍历或分配。

Frame热路径应维持有界sink数量和非阻塞工作；pool扫描上限由现有7 loans/64块/固定buffers约束。任何额外身份查询、CRC/复制和link验证都需DWT/trace测WCET；不能由O(N)小就声称满足帧率。PRIMASK只保护小型元数据，不包文件/队列等待，跨M4共享须遵循原IPC而不是误用M7关中断当双核锁。

主线SDMMC保留8KiB分段复制、1ms间隔和32KiB AXI所有权；StorageFS另有8KiB输入中转。该链路会占CPU/总线，graph/lifecycle重构不会自动提升25FPS，也不证明FE根因；本轮不改这些参数。任务通知可合并而队列持消息，不能由通知当DMA/Frame完成ACK。

## 5. 回归与实板要求（未来计划，NOT_RUN）

- 静态图：非法格式/link/资源范围、未支持ETH/WIFI明确拒绝、旧输出配置不变。
- 生命周期：正常/重复START STOP、失败回退、BUSY排空、ACK/FAULT独立重试、停止时仍持帧、超时不强收。
- 缓冲：JPEG单/双span、固定YUY2/RGB565/Y8 Capture、DMA M0/M1与IN_USE互斥、两个owner先后释放、SD-only内部角色、旧epoch/lease、队列满、迟到/重复Return。
- Storage：真实AVI Finish/Close/Unmount和错误结果、Snapshot/CRC、SD移除/Abort，文件结果与引用结果分离。
- 两核完整ARMCC5构建、原ABI/优先级/栈/启动顺序、Map/资源峰值、基线历史失败逐项对照；不降低断言。
- 实板：明确main V0与V1不同硬件基线，先获硬件适配授权再选择板；本轮没有V1新验收，不能拿B2B支线通过记录代替main。

## 6. 分支及本地工程隔离

为响应“支线output上传对应支线、不污染主工程”：C2.2B三份暂停草稿以WIP d815208备份到其原分支，FinalAcceptanceMatrix尚未完成，不能称已完成Final Contract Closure。Phase0五份研究只提交研究分支。

工作树已通过git worktree move移出主工程目录：

- 研究：D:/32/STM32H7/h7_mac_branches/architecture-media-graph-lifecycle
- C2.2B：D:/32/STM32H7/h7_mac_branches/stage-C2-2B-B2B6-v1-compat
- 原主目录output历史产物：D:/32/STM32H7/h7_mac_branch_artifacts（整体保留，不删除）

原D:/32/STM32H7/h7_mac目录当前为fix/v1-camera-i2c1，而非main工作树；没有擅自切换。其M4 main.c的HAL_Delay(500)和Keil配置修改仍在原目录，不复制进研究生产源码。主目录没有嵌套支线工程；main引用保持f0ce45f。

只提交对应支线的源码/文档/可复现测试证据。构建镜像、依赖和重复快照遵循.gitignore，不整体塞进Git；历史产物留本地隔离区。旧报告内绝对artifact路径是历史生成位置，移动不改写原始日志/哈希；访问时应用前缀映射：原output/B2B6/worktree→新B2B6工作树，原主目录output→branch_artifacts；嵌套快照优先匹配最长工作树前缀。本轮不重运行器或伪称旧路径仍可直接使用。

## 7. 评审决策

需要人工决定：静态拓扑schema是否接受；生命周期视图是否只做投影而非统一所有enum；资源适配先覆盖哪些已支持格式；如何将原Control路径单次切换；是否恢复/整合暂停C2.2B设计。没有这些决定，不实施Phase1或C2.2C。

完成本Phase0后停止等待审核。文件范围与源码定位见CurrentToTarget_Mapping；官方来源分别在前三文，不把自行提出的静态图、owner适配和迁移顺序归因给Linux/ROS/GStreamer。
