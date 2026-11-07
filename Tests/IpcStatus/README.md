# 阶段05C-2：IPC V3业务状态验收

从仓库根目录执行：

```powershell
python Tests/IpcV2/run_tests.py
python Tests/IpcRuntime/run_tests.py
python Tests/IpcRuntime/run_task_tests.py
python Tests/IpcStatus/run_tests.py
python Tests/IpcStatus/run_kernel_tests.py
python Tests/IpcStatus/run_build.py
```

ARMCC5.06u7、armlink、Keil、Python、Unicorn和pyelftools沿用原测试环境。编译器安装路径在脚本中，生成物放output/IpcStatus；不提交对象、AXF或本机运行库。

- 原05B 512次和原05C-1 260+20次全部保留。V3将应用载荷正式命名，旧测试仅同步feature_flags=3、reserved最后一项索引24→4；场景、编号和断言强度不降低。
- run_tests.py：真实Core/Service/Runtime/Application状态模块。CM7为128编号×O0/O2，CM4为96编号×O0/O2，共448次。CM4没有业务发布API，不重复执行CM7专属写入点。应用接口在纯C端口下执行，Core使用真实共享布局及真实序号算法。
- run_kernel_tests.py：真实FreeRTOS内核、任务身份、app_events、IPC Task和app_status。41次，其中20项是原BOOT/任务专项再次执行，21项为新增业务汇总、身份、临界区、回绕和采样Tick检查。
- 实际内存写钩子验证Application记录写入受到临界区保护；真实内核套件检查BASEPRI=0x50。模拟器不模拟物理NVIC抢占/SVC/PendSV异常返回。
- 编号说明见case_catalog.json；逐项结果见results.json、task_results.json；命令与版本见commands.json、task_commands.json；正式完整构建见build_results.json和两份rebuild.log。

本轮共1281次实际执行：512+260+20+448+41。最后41项包含已执行过的20项，不声称1281个不同测试场景。所有运行期新增硬件验证仍待用户实板反馈。
