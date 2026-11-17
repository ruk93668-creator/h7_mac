# Application V2 迁移与Legacy退休计划

基线 `8a03596`。本轮只设计；以下阶段不自动获得实施许可。开源源码研究和ADR随本计划一起审核，所有具体V2方案均为UNRESOLVED（推荐方案，待批准）。

## 1. 迁移纪律

保留旧Application作为当前参考，不能现在删除。V2在ApplicationV2独立开发；纯模块/模拟端口先验证，真实硬件只绑定一个应用。不开第二组真实Camera/USB/Storage任务，不把新旧Frame状态拼成一套实现。最终切换后逐组删除旧架构源和工程引用，历史由Git保存，无常驻Legacy目录。

每阶段输入为上阶段经审核接口和冻结源SHA；提交区分设计、纯代码、测试、工程切换和删除，便于独立回退。若生产行为需变化，先提交差异与批准请求。每项删除以“功能承接+无有效引用+链接/实板验证”为门禁，不能用grep未找到调用者代替链接证据。

## 2. 冻结Phase 0～10实施矩阵（2026-11-17同步）

阶段名称以本轮用户明确冻结路线为准；未完成阶段均待单独授权，不因同步文档获得编码许可。

| Phase | 名称 | 输入/主要交付 | 验收/回滚门禁 |
|---|---|---|---|
| 0 | Architecture Freeze | 五核心、目录、接口和安全约束 | 设计审批；不动Legacy |
| 1 | Core + Composition | Core、Registry、Resolver、Graph、Format、候选组合；501330a已实现 | bd24c03有条件验收；单owner/Mock不扩大为并发/硬件验收 |
| 2 | Media Resource | Raw/JPEG既有后端端口、Buffer Source、G/H/M关联、Grant/Lease、关闭、ACK/退休、DMA证据 | 两项NOT_PROVEN须真实软件/硬件证据；不得第二引用计数；未活动实现可独立回退 |
| 3 | Execution Manager | 基于资源事实的Managed Lifecycle、异步计划执行、Supervisor、真实结果与补偿 | 无同步互等；DECIDED/BLOCKED/原Replay保持；失败不伪造清理；回退整包 |
| 4 | Camera Module | Camera owner/采集端口、普通/保护性停止协调 | 唯一普通停止、DRAINING/失败/重启；活动实验回退整固件 |
| 5 | USB Module | USB业务及Resource交接适配 | 不改EP0/UVC；原IO ACK与后台恢复独立、真实Return/ACK；整包回退 |
| 6 | Storage Module | 录像/抓拍/文件与Resource槽位适配 | Finish/Close/Unmount/帧归还分开；保留异常文件，整固件回退 |
| 7 | Control Service | IPC/USB/BLE/Wi-Fi/Ethernet统一控制适配契约、请求和权限 | 传输能力缺失不伪造；断线不释放Lease；未实现网络保持Future |
| 8 | Runtime Reconfiguration | Desired/Applied/revision、真实Drain→重配置→激活与补偿、经审核单应用切换 | 候选计算不等于切换；完整两核/Map/ABI/栈/实板；完整版本回退 |
| 9 | Network / AI Extension | 经批准网络/AI模块及真实后端能力扩展 | 逐DMA安全与多owner；关闭扩展不损USB/SD |
| 10 | Legacy Cleanup | 逐文件完成承接、删除旧源/工程引用/头路径/导出 | V2唯一活动应用、完整回归和链接/实板；历史仅Git保留 |

Phase1基础算法不再列为Phase2主任务；Phase2可补产品必要能力声明但不得重写Graph/Resolver。Resource先于业务绑定，Camera先于USB/Storage；Execution负责协调不直控硬件。后续每阶段需独立输入SHA/审批与回滚提交；不擅调任务栈、优先级或队列。

## 3. 文件级退休映射（C与同名H一起审核）

| Legacy文件 | V2承接功能/阶段 | 删除前特有检查 |
|---|---|---|
| app_main | Core/bootstrap/8；最终清理10 | App_Init及APPLICATION导出只有一个 |
| app_tasks | Core/Runtime/3,8；最终清理10 | 五任务创建顺序、BOOT、失败记录、静态TCB只创建一次 |
| app_events | Runtime/3,8；最终清理10 | USB/Media task/ISR callback全部重新绑定，BOOT前pending、Attach和owner不丢 |
| app_messages | Runtime运输+Lifecycle兼容接口/3,7,8；最终清理10 | 五+三通道、源权限、BASEPRI初始化特例、运行期保护和52B/56B复制布局不变 |
| app_camera | Modules/Camera/4 | Device配置、原Init/Snapshot入口、普通/保护性STOP、诊断发布和真实失败结果完整承接 |
| app_camera_lifecycle | Lifecycle/Transaction/3 | 安全工具先做无算法变化迁移和回归，再删除旧物理文件；不保持双份实现 |
| app_camera_lifecycle_replay | Lifecycle/Transaction/3 | 高水位、候选隔离、Ready取消、编号单调、拒绝重入完整保留；不能先删除再重写 |
| app_camera_qualification及internal头 | Lifecycle资格+Camera端口/3,4 | 去除App内部裸访问不丢DECIDED/BLOCKED、generation和R2停止结果匹配 |
| app_frame_dispatcher | Graph数据描述已属1；Resource交接/2,6 | frame metadata及Storage同步Offer由新端口承接；Graph本身不代替Dispatcher执行 |
| app_uvc_bridge | Resource transfer+USB module配置/2,5 | 双端owner、旧Return ACK、取消/重放、SD-only原始持有、初始化导出完整迁移 |
| app_usb | Modules/USB/5 | 业务/资格停止独立、原ACK/FAULT和FPS诊断更新契约 |
| app_storage | Modules/Storage+Resource槽位适配/2,6 | 四请求与命令顺序、真实Release由Camera执行、文件/恢复和统计全部承接 |
| video_config | Configuration/1,8 | 默认模式/格式/速率/分区/预算与运行期更新拒绝有显式映射 |
| app_status | Diagnostics/IPC/7,8 | IPC字段/age/源owner/CM4兼容保持，诊断不再充当业务依据 |
| Tasks/camera_task | Runtime Camera角色/4,8 | BeginRound/Media/交接/Finish/ACK/FAULT顺序与栈配置 |
| Tasks/usb_task | Runtime USB角色/5,8 | Cancel/Process/Bridge/再次Process/Stop顺序、SOF统计 |
| Tasks/storage_task | Runtime Storage角色/6,8 | Prepare/IsReady、等待和栈采样 |
| Tasks/control_task | Execution协调/3；Control/7；重配置/8 | 用户命令、启动策略、超时/首错、IPC状态承接；不保留旧全局业务if/else |
| Tasks/ipc_task | Runtime IPC角色+Diagnostics/7,8 | IPC owner注入、应用载荷/心跳和CM4兼容；Service不删除 |

其余文件由全量审计39项清单作为删除工作包输入，头文件搜索路径、extern debug观察、测试include生产C的夹具需一起迁移。历史原始断言/FAIL保留；新V2测试独立，不为了删除文件破坏历史可复现性，可固定历史commit构建历史夹具。

## 4. Keil与链接清理门禁

先记录 `MDK-ARM/h7_mac.uvprojx` 两target、组/文件级IncludeInBuild、include顺序、宏、Initcall/Device导出段、scatter及源文件清单。逐项比较有效编译而非XML登记。切换时同时移除旧源引用、旧头路径、旧宏和初始化入口，检查重复符号及未解析符号；确保CM4仍使用自己的Application，CM7只使用V2。

允许未来分阶段退休“已无活动调用者”的旧组件；尚有必要功能未迁移时保留原源，不建立常驻Legacy文件夹。Phase10最终门禁必须达到旧CM7/Application活动源码与Keil引用均无架构残留，底层Media/Device/BSP和M4/OLED未被误删。删除提交与工程切换提交可分别审核，但不得发布无法构建的中间固件。

## 5. 验收、资源和回滚证据

每阶段：绑定源码SHA；host O0/O2记录真实调用及负对照；C入口与Python门禁分开；ID集合缺失/重复失败；FAIL/BLOCKED/CONTRACT_CHANGED/NOT_RUN不混用。保留B1 ID9/10已批准停止时序变化、B2 ID23/31的人工决策以及其他分支历史证据，不自动移植其测试结果。

正式集成：ARMCC5 CM7/CM4完整Rebuild，0 Error/Warning、AXF/MAP/HTM/HEX按原target生成策略保存和哈希；sizeof/offsetof验证52B/56B与相关结构；Map对比RW/ZI/常量位置，调用图与实测high-water验证栈。源代码算式不是Map实测，静态调用深度不是最坏栈测量。

现有task栈：Camera6144B、USB8192B、Storage8192B、Control3072B、IPC2048B，共27648B（按源码word×4计算，不含TCB）。本轮不修改；未来不能以资源紧张为理由擅增栈。V2预算按const图表、每端口对象、固定队列、关联/收据容量逐项sizeof，禁止把已有824B资格对象中664B Rx再重复计算。4个方向各一Tx/Rx的工具对象理论1904B，是否实例化及与现对象替换关系UNRESOLVED，不是本轮RAM增量。

真实板验收必须记录板型与固件SHA，分别验证上电/ID、raw/JPEG支持路径、USB/SD单路/双路、单路退出、最后退出、重启、Abort/Media错误/掉卡、DMA安全、连续运行和栈。主线BSP不是V1硬件适配；硬件基线先单独决策。
