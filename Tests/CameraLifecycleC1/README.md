# C1 生命周期协议专项

设计见 Docs/Design/C1_Camera生命周期协议基础.md；完整结果见 verification_report.md 和 results.json。

在仓库根目录运行：

```powershell
python Tests/CameraLifecycleC1/run_tests.py
python Tests/CameraLifecycleC1/run_regression.py
python Tests/CameraLifecycleC1/run_build.py
python Tests/CameraLifecycleC1/audit_scope.py
```

依赖工程现有ARMCC5、Keil、Unicorn及pyelftools。专项执行真实协议、消息适配及FreeRTOS queue.c/list.c，任务身份、调度出口及通知由桩控制，不触及实卡或USB。

419项：协议70；旧通道基线/候选轨迹60；新队列49；实际任务权限240。测试不会降低原断言。

回归在独立output源码快照执行，不修改历史测试。audit_scope.py针对本阶段固定基线，不是任意未来版本通用CI。完整编译输出、原输出备份和命令记录在output/CameraLifecycleC1；不提交固件。

当前业务Task没有消费新协议；实板保持原启停行为。不要手工投递新消息来期待Camera启动，C2尚未实施。
