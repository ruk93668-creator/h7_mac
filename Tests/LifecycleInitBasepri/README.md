# LifecycleInitBasepri

完整Diff、结果和实板复测见 [verification_report.md](verification_report.md)。

在仓库根目录执行：

```powershell
python Tests/LifecycleInitBasepri/run_tests.py
python Tests/LifecycleInitBasepri/run_protocol.py
python Tests/LifecycleInitBasepri/run_build.py
python Tests/LifecycleInitBasepri/audit_scope.py
```

需要原有ARMCC5、UV4及Unicorn依赖。新增专项使用真实FreeRTOS队列和实际消息适配器，Port/任务上下文为桩。测试不参与正式固件，不向真实SD卡写入。软件PASS不能当实板启动通过。
