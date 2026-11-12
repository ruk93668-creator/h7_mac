# CameraStopOwnershipR2

实施/验收说明：[`C2.2A-R2_Camera停止职责整改报告.md`](../../Docs/Design/C2.2A-R2_Camera停止职责整改报告.md)。

在仓库根目录依次执行：

```powershell
python Tests/CameraStopOwnershipR2/run_tests.py
python Tests/CameraStopOwnershipR2/run_original.py
python Tests/CameraStopOwnershipR2/run_protocol.py
python Tests/CameraStopOwnershipR2/run_regression.py
python Tests/CameraStopOwnershipR2/run_build.py
python Tests/CameraStopOwnershipR2/run_layout.py
python Tests/CameraStopOwnershipR2/audit_scope.py
```

需要本机 ARMCC5、Keil UV4 与既有 Unicorn 测试依赖。不得将软件桩视作硬件验证。`run_regression.py` 保留两个原 B1 轨迹 FAIL 并退出1，禁止过滤这些差异。运行回归期间不要修改已跟踪文件，脚本会检查工作区未被回归改写。

`stop_test.c`执行真实CameraTask/App_Camera/Media；BSP、Device、FIFO借用边界和Bridge使用原B1桩，只有BSP停止计数包装。`decision_test.c`执行真实资格核心及原Rx/Replay，通过既有测试故障钩子验证冻结语义。Fixture只是调度适配，没有复制生产停止算法。

输出只写入新的 `output/CameraStopOwnershipR2/`；不会清理其他output。独立测试不参与正式Keil构建。
