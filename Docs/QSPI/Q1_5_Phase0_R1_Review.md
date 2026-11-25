# Q1.5 Phase 0-R1 新主线差异审核

状态为 **PHASE0_R1_REVIEW_READY**。QSPI输入为 `5bdd963862431b490f7e12b21f94b8e7f19ac9e9`，新主线为 `62b42d7c0b7df9eadafa00953ee8ce1229da9826`，旧矩阵底座为 `f0ce45fd9ab7250b1e22260e087d606ee5ed9ecb`。本轮只审核冻结提交与修订文档，没有集成源码、修改工程、创建共存分支、构建或操作实板。

## 全量差异与证据

从本机已有main仓库通过提交对象导出冻结快照到 `output/QspiFlashQ1/q15_r1/reference/`。使用提交对象生成差异，未将其他工作树的未提交文件当作输入。QSPI仓库自身尚无新main对象，因此V3证据均明确来自冻结快照及相应GitHub blob，不把本地旧Application同名文件误标为V3。

对CM7、CM4、Common和MDK-ARM完整比较，并确认Board、Drivers、Middlewares没有变化，得到77条路径变更，覆盖全部36份V3生产文件、旧Application删除/重命名、12份其他生产C/H及Keil工程。完整原始差异保存为 [生产补丁](Phase0R1/old_to_v3_production.patch)，SHA-256为 `1fd2766d76cc1ec57db75a07c5826d3c1844da9cfb69adabb9a688ade41dd927`。逐文件作用与QSPI影响见 [77项审核表](Phase0R1/file_review.md)，旧/新内容SHA、非注释标记、未变依赖和工程语义见 [机器清单](Phase0R1/source_diff_audit.json)。

补丁按原始文件编码保留，没有清理原主线代码或改写历史注释。去注释/空白的C词法对比用于识别实质变化，不能替代行为测试。

## 旧方案必须修订的部分

|旧Phase 0假设|新冻结主线事实|R1修订|
|---|---|---|
|旧CameraTask/UsbTask/StorageTask独立业务入口|服务清单创建AppV3_CameraRun/AppV3_UsbRun/AppV3_StorageRun，共用AppV3_ServiceRun|矩阵所有旧任务引用替换为V3 Core/Services/Tasks真实入口和冻结行号|
|BOOT后即可认为业务运行|Control先订阅启动队列再放BOOT；服务真实发布后汇总BusinessReady；设备流仍需单独启动|QSPI试验门禁要求全部服务真实就绪及单独授权；不把空闲共存外推为采集/USB/SD活跃共存|
|控制/监督结果可由测试线程直接查询|Supervisor Set/Query/Recover/Read均为Control身份专用|独立QSPI测试线程不借用业务owner，不调用这些API、不伪造通知/命令|
|仅旧消息队列行为|新增静态池、代次票据、实例Record、单订阅事件循环和结果背压|测试不申请V3队列、不修改RuntimeTarget/池容量/回调；保留52/16/80 B ABI|
|软SCCB和原相机上电路径|I2C1 PB9/8、PA15 DVDD及PH13 PWDN等正式硬件变化|更新GPIO/RCC与初始化审核，加入相机I2C/电源回归，不沿用旧电平/时序结论|
|旧帧获取和UVC终结行为|通用消费者Lease、代次和身份、Reader Proof及取消/EOF/关闭证明|QSPI不能持有帧或SD卷资源；共存回归须覆盖物理回收和USB读者退出|
|旧AVI索引容量|索引最大65536项，固定1MiB SDRAM窗口|QSPI不占AVI/Camera SDRAM，需以新Map审计共存RAM|
|整个旧QSPI Keil工程可迁移|旧QSPI工程以旧Application为底座且登记m7_project_test|只以新main工程选择性登记CM7 HAL/BSP，保留V3和两核隔离配置|

原有GPIOF/G、QUADSPI独占、D1HCLK和TIM6契约继续适用，但实际同步HAL对V3消息/Lease处理的时延必须重新测量。产品明确只有BSP，不增加QSPI Service、消息队列或Supervisor组件。

## 未变资源和工程复核

main.c、Framework初始化、FreeRTOSConfig、TIM6 timebase、SDRAM、DCMI、SDMMC、USB BSP、sd_block、storage_fs及80 B IPC头共11个关键依赖文件与旧main逐字节相同，CM4全树无变化。未引入新的PLL、IRQ、MPU、HSEM或IPC协议配置。Camera BSP和USB链路/Reader等变化则逐项按实际token变化审核，未归类为纯注释改动。

Keil目标树均有跨核显示文件登记，不能只数File节点就判定编译变化。CM4节点数由170到177，但V3四组IncludeInBuild=0，实际启用C/ASM仍41份，TargetOption逐项XML相同。CM7实际启用C/ASM由94到92，更新为V3路径；公共IncludePath从旧Application改Core，Supervisor链接keep新增，Scatter路径未改，Cads产品Define相同。机器清单同时列出所有登记项与有效编译项，A阶段仍需重建核对真实Map。

## 同步调用和剩余阻塞

独立测试任务的768 words/优先级1、256 B短读/20ms总预算及至少10ms显式让出均为待审核参数。不能把BSP的短PRIMASK、可被高优先级任务抢占等同于实时无影响。Storage与IPC同级轮转、BUSY等待、Frame Lease归还、Reader退出、AVI写入尾延迟和Control结果背压需形成对照。正式五任务栈大小/优先级不因QSPI改变，测试任务只采样自身水位；Control的历史最低栈查询保持原权限。

新main正式BOOT后业务默认未启流。A可以在未改生产任务的隔离夹具中验证软件路径及空闲共存，但B的实际活跃业务需明确已批准的Control产品入口。当前任务禁止为测试插入生产业务逻辑，也未批准新IPC控制协议，因此该项列为B前置阻塞。不能借测试任务调用Supervisor或直接操作Service绕过。

H1/H2历史文档、原始捕获及旧审计包不变。当前板上最后下载仍为H2的用户确认沿用，本轮没有读取板卡。下载前取消内部Flash备份等证据缺口保持。任何相机恢复或共存镜像下载需独立范围与回退审核。

## 本轮文档差异和核验范围

修改 [兼容性矩阵](Q1_5_Phase0_Compatibility.md)和 [实施计划](Q1_5_Phase0_Plan.md)，新增本报告、逐文件审核、冻结源内容哈希、Keil语义对比及完整原始生产差异。旧H1/H2证据与产品源码均未修改。

本轮验证只包含冻结Git对象读取、77项差异覆盖、原始补丁SHA、XML/IncludeInBuild、C词法和20处冻结源码链接的存在性/行号范围核验；当前矩阵与计划中的旧Application/Tasks路径为零；软件回归、正式构建、性能和实板测试全部NOT_RUN。新回归矩阵明确指定V3 O0/O2、MediaLease35、USB/SD联合6、历史FIFO3 FAIL等目标，不引用旧PASS代替实施后结果。

等待GPT审核R1方案并决定是否准入Q1.5-A。
