# Application V2 迁移与Legacy退休计划

基线 `8a03596`。本轮只设计；以下阶段不自动获得实施许可。开源源码研究和ADR随本计划一起审核，所有具体V2方案均为UNRESOLVED（推荐方案，待批准）。

## 1. 迁移纪律

保留旧Application作为当前参考，不能现在删除。V2在ApplicationV2独立开发；纯模块/模拟端口先验证，真实硬件只绑定一个应用。不开第二组真实Camera/USB/Storage任务，不把新旧Frame状态拼成一套实现。最终切换后逐组删除旧架构源和工程引用，历史由Git保存，无常驻Legacy目录。

每阶段输入为上阶段经审核接口和冻结源SHA；提交区分设计、纯代码、测试、工程切换和删除，便于独立回退。若生产行为需变化，先提交差异与批准请求。每项删除以“功能承接+无有效引用+链接/实板验证”为门禁，不能用grep未找到调用者代替链接证据。

## 2. 十阶段实施矩阵

| 阶段 | 输入依赖 | 输出接口/拟修改文件 | 测试范围及回归 | 验收条件 | 回滚 |
|---|---|---|---|---|---|
| 1 V2 Core | 本轮架构/ADR批准、板型冻结 | ApplicationV2/Core值类型/运行端口/BOOT契约；暂不替换入口 | 上下文、权限、队列满、通知合并、BOOT失败；旧应用回归 | 无动态分配/重复owner；ABI布局证据；Legacy仍唯一活动 | revert纯V2提交，不改Legacy |
| 2 Static Media Graph | Core与静态ID规范 | MediaGraph、Configuration/product_graph只读表 | 重复ID、坏端点、方向、重复link、循环、未实现sink、非法Pipeline | 有界验证；无硬件/Frame副作用 | 移除未活动Graph登记 |
| 3 Media Format | Graph/现有格式能力 | media_format和显式适配，不改CameraFrame/消息 | Y8/RGB565/YUY2/JPEG、两span、溢出、对齐、分辨率、FS带宽限制 | 能力取交集，拒绝转换；保留原raw采集能力 | revert格式适配，旧格式路径不变 |
| 4 Resource Manager | Core/Format，精确Grant关联/关闭/退休API审核 | Resource的Ports/Association/Admission/Transfer；如需Media只读查询另行授权 | O0/O2：真Get/Retain/Release、错owner/重复/迟到、队列满、SD-only、raw与JPEG；DMA不再访问 | Grant→Loan与稳定关闭有源码+运行证据；无第二计数；无强制回收 | 未接硬件时revert；若已绑定则整套旧固件回退 |
| 5 Managed Lifecycle | Resource退出证据；受控事务工具迁移审核 | Lifecycle状态/资格/Transaction适配、只读结果 | C1/C1.1/C2.1.1/C2.2A/R2等效轨迹；DECIDED/BLOCKED、BUSY重入/NO_RESOURCE、编号耗尽 | 冻结结果与资源事实一致；Replay规则不变；无同步互等 | 回退本阶段V2，无清身份捷径 |
| 6 Camera Module | 4/5与设备端口 | Modules/Camera；Core/Runtime的Camera角色 | START部分失败/同轮保护、BUSY、DRAINING、失败后台恢复、重启、真实停止事实 | 普通停止单入口；Media停止与Device/Bridge独立；无重复下发 | 活动实验需整固件回退，不混用旧App_Camera |
| 7 USB Module | Camera与Resource交接 | Modules/USB及USB角色调度 | UVC正常/Cancel/Abort、Return/ACK、Reset/Disconnect/Suspend、TEST/NONE、ID7失败后台释放、ID23/31 | 不改Core/EP0/UVC；原STOP错误不丢、无假RELEASE | 回退完整USB/Camera实验构建 |
| 8 Storage Module | Camera与Resource、原StorageFS/AVI | Modules/Storage及Storage角色调度 | 录像/抓拍、部分写入/CRC、Sync/Finish/Close/Unmount失败、槽位竞争、回收/掉卡；B3契约 | 文件/清理/帧/资格四结果独立；不修改SDMMC参数 | 保留异常文件；完整固件/配置回退 |
| 9 Supervisor | 模块联合通过、产品策略单独审核 | Supervisor/Configuration、V2 bootstrap/Runtime/Diagnostics/IPC；经批准切换Keil | 同时冷启动、单路停另路继续、全Pipeline排空、队列满/迟到、用户命令/状态IPC、完整双核回归 | 一个活动Application；唯一owner；两核Rebuild、Map、ABI、栈及目标板实测；随后按清单退休旧文件 | 回退切换/清理完整提交到保留的历史SHA，不能只回退一个头文件 |
| 10 Network Extension | V2稳定与独立需求批准 | 未来Modules/Network及Driver Port/const图扩展 | Ethernet/Wi-Fi控制面/数据面、零拷贝/复制、多DMA、背压/丢帧/重连；可选AI | 后端支持真实新owner；控制/数据面隔离，异步完成可证明 | 网络功能整体关闭，不影响USB/SD；旧网络无权释放其他owner |

阶段2/3的纯描述与测试设计可并行；资源关联必须在业务接入前完成；Camera先于USB/Storage，USB与Storage只在共享契约冻结后可独立开发。Supervisor计划执行在模块完成接口稳定后实施。Network/AI不是本轮需求实现，绝不为其预先改变当前Replay或消息ABI。

## 3. 文件级退休映射（C与同名H一起审核）

| Legacy文件 | V2承接功能/阶段 | 删除前特有检查 |
|---|---|---|
| app_main | Core/bootstrap/9 | App_Init及APPLICATION导出只有一个 |
| app_tasks | Core/Runtime/1,9 | 五任务创建顺序、BOOT、失败记录、静态TCB只创建一次 |
| app_events | Runtime/1,9 | USB/Media task/ISR callback全部重新绑定，BOOT前pending、Attach和owner不丢 |
| app_messages | Runtime运输+Lifecycle兼容接口/1,5,9 | 五+三通道、源权限、BASEPRI初始化特例、运行期保护和52B/56B复制布局不变 |
| app_camera | Modules/Camera/6 | Device配置、原Init/Snapshot入口、普通/保护性STOP、诊断发布和真实失败结果完整承接 |
| app_camera_lifecycle | Lifecycle/Transaction/5 | 安全工具先做无算法变化迁移和回归，再删除旧物理文件；不保持双份实现 |
| app_camera_lifecycle_replay | Lifecycle/Transaction/5 | 高水位、候选隔离、Ready取消、编号单调、拒绝重入完整保留；不能先删除再重写 |
| app_camera_qualification及internal头 | Lifecycle资格+Camera端口/5,6 | 去除App内部裸访问不丢DECIDED/BLOCKED、generation和R2停止结果匹配 |
| app_frame_dispatcher | Graph数据描述+Resource交接/2,4,8 | frame metadata及Storage同步Offer由新端口承接；Graph本身不代替Dispatcher执行 |
| app_uvc_bridge | Resource transfer+USB module配置/4,7 | 双端owner、旧Return ACK、取消/重放、SD-only原始持有、初始化导出完整迁移 |
| app_usb | Modules/USB/7 | 业务/资格停止独立、原ACK/FAULT和FPS诊断更新契约 |
| app_storage | Modules/Storage+Resource槽位适配/4,8 | 四请求与命令顺序、真实Release由Camera执行、文件/恢复和统计全部承接 |
| video_config | Configuration/3,9 | 默认模式/格式/速率/分区/预算与运行期更新拒绝有显式映射 |
| app_status | Diagnostics/IPC/9 | IPC字段/age/源owner/CM4兼容保持，诊断不再充当业务依据 |
| Tasks/camera_task | Runtime Camera角色/6,9 | BeginRound/Media/交接/Finish/ACK/FAULT顺序与栈配置 |
| Tasks/usb_task | Runtime USB角色/7,9 | Cancel/Process/Bridge/再次Process/Stop顺序、SOF统计 |
| Tasks/storage_task | Runtime Storage角色/8,9 | Prepare/IsReady、等待和栈采样 |
| Tasks/control_task | Supervisor计划/9 | 用户命令、启动策略、超时/首错、IPC状态承接；不保留旧全局业务if/else |
| Tasks/ipc_task | Runtime IPC角色+Diagnostics/9 | IPC owner注入、应用载荷/心跳和CM4兼容；Service不删除 |

其余文件由全量审计39项清单作为删除工作包输入，头文件搜索路径、extern debug观察、测试include生产C的夹具需一起迁移。历史原始断言/FAIL保留；新V2测试独立，不为了删除文件破坏历史可复现性，可固定历史commit构建历史夹具。

## 4. Keil与链接清理门禁

先记录 `MDK-ARM/h7_mac.uvprojx` 两target、组/文件级IncludeInBuild、include顺序、宏、Initcall/Device导出段、scatter及源文件清单。逐项比较有效编译而非XML登记。切换时同时移除旧源引用、旧头路径、旧宏和初始化入口，检查重复符号及未解析符号；确保CM4仍使用自己的Application，CM7只使用V2。

允许未来分阶段退休“已无活动调用者”的旧组件；尚有必要功能未迁移时保留原源，不建立常驻Legacy文件夹。阶段9最终门禁必须达到旧CM7/Application活动源码与Keil引用均无架构残留，底层Media/Device/BSP和M4/OLED未被误删。删除提交与工程切换提交可分别审核，但不得发布无法构建的中间固件。

## 5. 验收、资源和回滚证据

每阶段：绑定源码SHA；host O0/O2记录真实调用及负对照；C入口与Python门禁分开；ID集合缺失/重复失败；FAIL/BLOCKED/CONTRACT_CHANGED/NOT_RUN不混用。保留B1 ID9/10已批准停止时序变化、B2 ID23/31的人工决策以及其他分支历史证据，不自动移植其测试结果。

正式集成：ARMCC5 CM7/CM4完整Rebuild，0 Error/Warning、AXF/MAP/HTM/HEX按原target生成策略保存和哈希；sizeof/offsetof验证52B/56B与相关结构；Map对比RW/ZI/常量位置，调用图与实测high-water验证栈。源代码算式不是Map实测，静态调用深度不是最坏栈测量。

现有task栈：Camera6144B、USB8192B、Storage8192B、Control3072B、IPC2048B，共27648B（按源码word×4计算，不含TCB）。本轮不修改；未来不能以资源紧张为理由擅增栈。V2预算按const图表、每端口对象、固定队列、关联/收据容量逐项sizeof，禁止把已有824B资格对象中664B Rx再重复计算。4个方向各一Tx/Rx的工具对象理论1904B，是否实例化及与现对象替换关系UNRESOLVED，不是本轮RAM增量。

真实板验收必须记录板型与固件SHA，分别验证上电/ID、raw/JPEG支持路径、USB/SD单路/双路、单路退出、最后退出、重启、Abort/Media错误/掉卡、DMA安全、连续运行和栈。主线BSP不是V1硬件适配；硬件基线先单独决策。
