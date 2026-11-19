# Legacy Lifecycle 历史测试归档

三条 C1 运输队列由历史夹具显式初始化。C1、C1.1、C2.1.1 与 BASEPRI 通过原 C1 运行器选择归档队列。保留原测试编号和断言。旧提交的对照源继续从原 Git 对象读取。

`Adapter/v2_round_unbound.c` 由 C2.2A 与 R2 独立夹具链接，表示 V2 端口未绑定时没有轮次业务。实际 V2 调度由 P8.2 专项独立验证。

共享协议、Replay 与 generation 仍由产品的 V2 消费者使用，位置保持在 `CM7/Application`。归档目录没有产品 Keil 登记。

## C2.2A 与 R2

- [C2.2A](C2.2A/README.md) 收录核心、Camera 适配、协议和布局夹具。
- [R2](R2/README.md) 收录停止、终结决定、原始回归和最终组合验收证据。

在隔离的仓库快照中执行，尤其是 Keil 的 `run_build.py`，以保护产品工程的当前输出。主入口为：

```powershell
python Tests/LegacyLifecycle/C2.2A/run_tests.py
python Tests/LegacyLifecycle/R2/run_tests.py
```

归档目录内的 JSON、构建日志和历史验收记录用于追溯。新结果写入 `output/LegacyLifecycleArchiveValidation/`。旧专项与原版本及产物绑定，较新基线可能触发原门禁；不得修改断言或将历史失败改判通过。
