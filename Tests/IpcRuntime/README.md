# 阶段05C-1：IPC运行期验收

> 本分支05C-2的当前生产代码为V3。本套件继续执行原05C-1的260项运行期及20项Task回归，未把历史V2报告改写为V3实板结果。

基线为dfa9bfc。正式实现使用真实ipc_core.c、ipc_runtime.c及两核ipc_service.c；独立ARMCC5编译并由Unicorn执行ARM指令。Task专项另使用真实FreeRTOS内核、app_tasks、app_events及ipc_task。

在仓库根目录执行：

```powershell
python Tests/IpcV2/run_tests.py
python Tests/IpcRuntime/run_tests.py
python Tests/IpcRuntime/run_task_tests.py
python Tests/IpcRuntime/run_build.py
```

ARMCC5.06u7和Keil路径沿用本工程安装位置；Python需要Unicorn及pyelftools，优先复用现有Tests/OledAsyncAcceptance/python_deps。本机依赖、AXF、Object均写入output或使用已有安装，不随提交上传。

- 原05B：128场景×两核×O0/O2=512次。原测试主体与断言保留，仅补充CM4 Timer和Tick依赖桩，编译加入真实运行期文件。
- Runtime：65场景×两核×O0/O2=260次。编号与角色差异见case_catalog.json，逐项结果见results.json。真实Service诊断副本的每次内存写入还检查是否位于模拟临界区内。
- Task：20项，逐项说明及结果见task_results.json。真实内核任务创建/通知、4目标BOOT广播、身份判断和实际IPC任务入口均执行。
- build_results.json和两份rebuild.log是正式双核完整重编证据。

Task模拟器显式选择当前任务和内核运行态，不实现SVC/PendSV异常返回；无限任务在首次等待/延时API入口停止。它验证调用环境和100ms阻塞参数，不能证明物理NVIC抢占或任务实际调度周期。Runtime模拟使用可控本地Tick和真实共享状态，不证明双核总线时序。所有新增运行期实板项目均待验收。
