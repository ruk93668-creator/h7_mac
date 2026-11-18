# Phase 6 Storage Module 实施计划

输入基线 aea46f2，研究分支 codex/architecture-media-graph-lifecycle。该基线没有独立的 Phase 6 ImplementationPlan。本计划落实 FrozenRoadmapSynchronization、MigrationPlan Phase 6 行和 Phase 5 Legacy Adaptation Plan 的 Storage 前置契约。

1. 独立格式与 Backend 适配。保留 CoreMediaFormat，JPEG 双 Span 经过已有验证器，复用 StorageFS 和 AVI 服务。原 FatFS、SD Block、SDMMC 保持源码和调度。
2. 独立 Lifecycle、Endpoint 与 Camera 交接。存储 owner 执行文件操作，Camera owner 执行 MR / 物理帧操作。单槽位、不允许回绕，G、R、H、M、S、T 独立。USB 的 B 不用于 Storage。Return、ACK 入队、消费、退休分开验证。
3. ARMCC5 O0/O2 联合测试、负对照、现有回归、原 Keil 双核隔离 Rebuild。记录 C 文件构建、真实服务与替身边界、Map、静态预算和所测调用栈。
4. 原应用和真实物理 Backend 的接线单列适配清单。缺少权威归还、闭口、IDMA 和真实任务证据时记录 NOT_PROVEN/NOT_RUN，禁止进入 Phase 7。

本阶段单会话单文件，AVI 由文件尺寸上限及服务自身帧数上限约束，抓拍为单帧。旧录像滚动分段、文件命名策略、事件入口切换和实板带宽待真实适配审核。失败文件原样保留。
