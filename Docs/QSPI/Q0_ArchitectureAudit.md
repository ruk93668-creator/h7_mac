# Q0 架构补充审计

日期：2026-11-21（Asia/Shanghai）。阶段：Q0；总体状态：NOT_VERIFIED。
源码证据统一基于 `f0ce45fd9ab7250b1e22260e087d606ee5ed9ecb`，行号指该提交；本轮仅新增五份审计文档，未实施 Q1。
文中“事实”为实际检查结果，“建议”为待批准设计，“未验证”为尚缺证据；建议不能视为已有功能。

## 1. Git 与工程环境（事实）

| 项目 | 实际结果 |
| --- | --- |
| 工作目录 | `D:\32\STM32H7\h7_mac_qflash` |
| 当前分支 | `codex/qspi-flash` |
| 审计起点 HEAD | `f0ce45fd9ab7250b1e22260e087d606ee5ed9ecb` |
| origin | `https://github.com/ruk93668-creator/h7_mac.git`，fetch/push 相同 |
| 上游 | `origin/codex/qspi-flash`；起点领先/落后 0/0 |
| 起点工作区 | `git status --porcelain=v1` 无输出 |
| fetch 后 main / QSPI 远端 | `git ls-remote --heads` 两者均为上述完整 SHA |
| 本克隆 worktree | 仅当前目录，非其他克隆的 linked worktree |
| 本地分支 | main、codex/qspi-flash，起点均为 f0ce45f |
| QSPI 远端分支搜索 | `git branch -r --list '*qspi*' '*qflash*'` 仅当前支线 |

已执行 `git fetch origin`、`git remote -v`、`git rev-parse HEAD` / `origin/main`、`git branch -vv`、`git worktree list --porcelain`、`git log -1 --format=fuller origin/main`。main 提交日期 2026-11-14 17:06:43 +0800，主题为合并 PR #6（consumer qualification core）。这些是本轮观察快照，并非远端永不变化的保证。

另对已知相邻仓库执行只读检查：

| 工作树 | HEAD / 状态 |
| --- | --- |
| `D:\32\STM32H7\h7_mac` | `a6f6b22787fd080f3defbeb4bedcc1fd5e845f1b`，codex/application-v3-flipper；uvoptx、uvprojx 修改，Docs/Architecture/V2/、output/ 未跟踪 |
| `D:\32\STM32H7\h7_mac-application-v3` | detached `6315148b7baf2ec88e4c263e793d2bc72cd93a88`；uvoptx、uvprojx 修改 |

二者共享另一个 Git worktree 注册表；未清理、覆盖、切换或提交其文件。其余兄弟克隆是否活跃、本机所有并行开发状态：未验证。fetch 观察到远端 application-v3 为 1800f1f，与相邻本地 HEAD 不同，QSPI 不引用其接口。

## 2. 主线实现与边界（事实）

| 证据（路径:行） | 核验结论 |
| --- | --- |
| `CM7/BSP/bsp_sdmmc.c:23`、`:29`、`:35`、`:41` | 私有 static HAL 句柄、诊断和占用状态；不将 HAL 句柄交给业务层 |
| `CM7/BSP/bsp_sdmmc.c:651`、`:670`、`:697` | 校验 ISR/PRIMASK/BASEPRI/TIM6 条件；短临界区获取操作权；映射 HAL 返回及错误位 |
| `CM7/BSP/bsp_sdmmc.h:1`、`:43`、`:99` | 头文件保护、独立诊断结构、初始化与 I/O 诊断分离 |
| `Common/Inc/sys_result.h:10` | SysResult_t 为 int32_t；成功 0，失败负值，禁止按位合并错误 |
| `CM7/Platform/sd_block.c:10`、`:19`、`:40`、`:75` | 静态二值信号量用于事件，TaskHandle_t 绑定所有权；故障/活动事务未解除时拒绝释放 |
| `CM7/Platform/sd_block.c:252` | Prepare 创建静态信号量并登记等待适配，末尾注册 PLATFORM Initcall；不在此识卡 |
| `CM7/Platform/sd_block.h:23` | Claim/Release/IsOwner 的调用者约束在 Platform，非公共 HAL 接口 |
| `CM7/Platform/storage_fs.c:13`、`:21`、`:31`、`:37`、`:43`、`:46`、`:226` | 固定文件池、静态输入缓存、忙标志和租约；显式 Recover；不复制 FatFS 业务到 QSPI |
| `CM7/Framework/framework_init.c:23`、`:40` | 扫描既有表，按 PLATFORM→BUS→DEVICE→MEDIA→SERVICE→APPLICATION 执行，遇错返回 |
| `CM7/Framework/framework_init.h:29` | `.framework_init` 静态注册，不需要另建 QSPI 初始化调度器 |
| `CM7/Core/Src/main.c:174`、`:179`、`:194` | 先创建任务，再执行 InitRuntime，最后启动 RTOS；Initcall 尚无任务调度 |
| `CM7/Platform/FreeRTOSConfig.h:26` | 静态分配 1、动态分配 0、Mutex/递归 Mutex/计数信号量 0；不等于禁用二值信号量 |
| `CM7/Platform/platform_hal_timebase.c:4`、`:9`、`:47` | TIM6 提供 HAL 毫秒 Tick，优先级 4；ISR 增加 Tick |
| `CM7/Platform/FreeRTOSConfig.h:50` | RTOS 系统调用优先级阈值 5；不能在 TIM6 ISR 调 RTOS API |

## 3. 编译、链接及 HAL（事实）

- QSPI HAL `.c/.h` 位于 `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_qspi.c` 和 `Inc/stm32h7xx_hal_qspi.h`，实际存在。
- `CM7/Core/Inc/stm32h7xx_hal_conf.h:67` 模块开关被注释；`:417` 仅在开关启用时包含头文件。`:207` 回调动态注册为 0。
- XML 全文搜索无 QSPI 条目。`MDK-ARM/h7_mac.uvprojx:3091` 为 CM7 target，`:3742`、`:4759` 可分别参照 HAL SD 与 BSP SDMMC 文件分组。CM4 target 始于第 10 行；禁止把共享工程文件的 CM4 段一起更改。
- 实际 Scatter 配置在工程 `:373` / `:3454`，分别为 `h7_mac_CM4_custom.sct` / `h7_mac_CM7_custom.sct`。CM7 脚本第 1、10、22、27、32 行描述内部 Flash、Initcall、共享 RAM、SDMMC DMA RAM、DTCM；未给 QSPI memory mapped 区域分配段。CM4 使用独立内部 Flash/RAM。
- `uAC6=0`（工程 `:14`、`:3095`）为当前工程配置；本机发现 `D:\32\UV4\UV4.exe`、`D:\32\ARM\ARMCC\bin\armcc.exe`。存在不等于编译器、许可证和探针可运行，本轮不构建、不烧录。

## 4. 风格与实施建议（未执行）

SDMMC/Storage 使用四空格和另起一行大括号；部分历史 Framework/SDRAM 使用 Tab，单句 if 有不加大括号的情况，不能声称全仓统一缩进。新 BSP 建议跟随 SDMMC：`BSP_QSPI_*` 公共函数、`BspQspiDiagnostics_t` 类型、`Qspi_*` 私有帮助函数、`s_*` 静态状态、`SYS_ERR_*` 返回值，中文注释说明调用条件、资源所有权与故障保留原因。源码同时存在旧中文编码与 UTF-8，终端部分中文乱码；不批量转码已有文件。

Q1 只做 BSP 和必要 HAL/工程接入；Q2 才形成 Platform 访问所有权，Q3 参数持久化，Q4 配置 Service。Q1 单个合法任务显式调用，BSP 自身还必须防重入；不能依赖 HAL 内部锁代表跨命令事务互斥。短临界区仅提交状态，HAL 轮询期间开中断，不引入堆、RTOS Mutex、新任务生产框架或 ApplicationV3 依赖。

## 5. 未验证与结论

架构与本地隔离检查已完成；没有运行 QSPI 固件，也未证明任何 Flash 通信。板级网络、运行时频率、器件原始内容和实板共存均未验证。详细边界见 Q0_Q1ImplementationPlan.md；总体 Q0 保持 NOT_VERIFIED，不构成 Q1 开发批准。

2026-11-21 补交资料更新：七页 SVG 和随后取得的 PCB 证据见 HardwareAudit §6–§8，已核验设计资料的 QSPI 六网、U5 footprint 和电源外围。EP 可悬空或接 GND 的原厂规范已核实，原理图 NC 属允许方案；送审 PCB 中心焊盘无 NET，未发现 Via 相交。实际生产板版本一致性、封装实装、焊盘网络/过孔/贴装、供电和通信仍未验证。未修改生产源码、未运行固件；Q0 整体仍 NOT_VERIFIED，Q1 未批准。
