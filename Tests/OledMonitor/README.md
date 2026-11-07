# CM4 OLED Monitor / Stage05D

在工程根目录运行：

```powershell
python Tests/OledMonitor/run_tests.py
python Tests/OledMonitor/run_regressions.py
python Tests/OledMonitor/run_build.py
```

需本机ARMCC5/UV4与Unicorn、pyelftools。测试直接编译实际源码；不连接硬件。
结果见[完整验收报告](verification_report.md)。仿真预览preview_*.png使用模拟IPC输入，从真实执行的Framebuffer导出，不是实板验收。
原测试断言不修改；回归归档器保存本次证据后还原历史报告。
