# 单帧联调历史说明

本目录保留过去的单帧采集统计、Watch记录、模拟结果和实板验收报告，不属于当前正式固件。

历史版本曾通过 `CameraCapture_GetLastStopResult()` 向上层查询停止恢复结果。2026-10-20按用户要求删除该设计：当前正式代码、历史源码副本和测试调用均不再保留该接口，底层停止与恢复由Media内部处理。

`diagnostics/` 仅保存独立联调测试需要的历史入口、统计模块、Media状态机和当时启用彩条的Device快照。Device快照与清理前基线哈希一致；当前正式Device后来关闭彩条，历史测试不会修改这一设置。为保留旧错误场景的验证，测试桩观察BSP Stop返回值并记录首次结果；Application中没有恢复结果查询接口。该观察器只在Tests中编译，不加入Keil正式Target。

旧的implement.py、prepare_tests.py和write_report.py是一次性迁移/生成工具，现已停用，避免重建过时接口或覆盖历史验收报告。运行历史回归仍使用：

```powershell
python Tests/CameraSingleFrame/Camera/run_tests.py
```

历史报告中的“曾新增该查询接口”只描述当时版本，不代表当前正式接口。当前正式Application/Media由 `Tests/CameraCleanup/Camera/run_tests.py` 验证。
