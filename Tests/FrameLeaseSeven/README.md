# 七缓冲与JPEG双路租借专项

替代旧08D副本池专项中的复制/源帧归还后可覆盖假设；旧记录保持历史用途，不把旧150项结果作为当前租借架构的验收。真实生产C源码由ARMCC5编译，Unicorn执行，文件介质/硬件为Mock。无本轮实板验收。USB与SD各最多借一张。YUY2多READY保留、最新帧交付；JPEG完整帧描述预算7，保留1个余量。SD在入队、写入及等待Camera确认归还期间都算占用，不能提前接受第二张。

依赖ARMCC5、pyelftools、Unicorn；PYTHONPATH可使用原工程Tests/OledAsyncAcceptance/python_deps。不提交本机依赖库或构建产物。

```powershell
python Tests/FrameLeaseSeven/run_tests.py
python Tests/FrameLeaseSeven/run_media.py media_test --count 96
python Tests/FrameLeaseSeven/run_media.py drain_test --count 5
python Tests/FrameLeaseSeven/run_case.py parser_test --count 16
python Tests/FrameLeaseSeven/run_case.py bridge_test --count 35
python Tests/FrameLeaseSeven/run_config.py config_test --count 8
python Tests/FrameLeaseSeven/run_tasks.py
python Tests/FrameLeaseSeven/validate_avi.py
python Tests/IpcRuntime/run_tests.py
python Tests/SdmmcStage08B/run_regressions.py
```

run_build.py按用户当前Keil配置重建两核，不修改Target。config_test实际编译用户video_config.c并执行Device/Media校验、真实Core/Probe/Commit/端点流程；不可达硬件桩的调用计数必须为0，不能把它当传感器初始化验证。

parser_test对原解析器场景保持断言，向测试JPEG增加合法COM段，使16KiB块内不足8帧，跨界帧不被正常READY淘汰策略先丢掉；代码不改变JPEG熵数据。stream_test_seven将原90项模型容量和非法索引边界更新到7，不删用例、随机操作或状态断言。长环回测试提高仿真指令预算，必须执行到真实返回地址。

实板步骤见hardware_steps.md，设计见Docs/Design/Stage08D_七缓冲与双路租借.md。此前DMA FE和复杂画面IPC调度延迟均保留待实板复验，软件PASS不能消除这些风险。

最终规则调整：原90个固定长度场景保留，独立参考模型改为多READY及FREE余量；丢帧期望按实际旧READY回收次数更新，原“额外READY非法”的场景改为“未知状态非法”，并新增最新交付后不能回退旧帧、序号回绕、稳定态角色计数及FREE不得耗尽等6个场景。变化来自用户明确选择的新契约，不是放宽硬件检查。旧历史测试原文件保留。
