# B3 Storage 业务权威状态整理

基线：`d725a698203b3bcde479a290714a75b16bc9de16`。只修改三个已有生产文件；不增加 Task、ACK/FAULT、FinishRound 或新的停止策略。

## 接口与状态

- `App_StorageIsReady()`：由 StorageTask 在原 ready 门控位置调用，只返回已有私有 `s_prepared`，不更新 Debug、缓存、消息或硬件。
- `s_business.saved_frames`：唯一供业务 FPS 计算使用的累计保存帧数。原两个成功计数位置先推进私有值，再投影到原诊断字段。
- `s_business.file_number`：成功创建文件后保存的编号。Snapshot 回读使用它重建文件名，诊断字段不参与选文件。
- 状态、槽、队列、文件、AVI、session/generation、归还凭据继续保留原实现；没有建立第二套状态机或 Media 引用账本。
- `g_storage_status` 布局、原诊断写入位置、缓存发布时间不变。Task 的等待错误、准备结果及栈余量依旧按原顺序写入。

## 保持的调用关系

```text
StorageTask：BOOT → Prepare → IsReady → Process → 每1000ms扫描栈
Process：一条命令 → 一张帧 → JPEG/CRC → 文件操作 → 标记待归还
         → 停止收尾 → 无帧超时 → FPS → 发布缓存 → 必要时自通知
CameraTask：BridgeCameraProcess → StorageProcessReturns → ReleaseLease
```

Storage IDLE 不证明 Media 租约已经归还；文件结束和 Camera 实际 Release 是不同阶段。正常 STOP、异常 Close、Recover 的原策略均未改变。

## 复现软件检查

在仓库根目录运行，依赖现有 ARMCC5、Keil、Unicorn 和 pyelftools：

```powershell
python Tests/StorageModuleB3/run_tests.py
python Tests/StorageModuleB3/run_regression.py
python Tests/StorageModuleB3/run_build.py
python Tests/StorageModuleB3/audit_scope.py
```

`run_tests.py` 编译基线和候选的真实 Storage/Task/JPEG/AVI；StorageFS 介质、Media 租约边界和 RTOS 使用测试桩。正常输入逐条比较依赖调用、参数、内容摘要、诊断及业务现场。诊断篡改用例明确验证预期差异，不混入行为等价统计。

`run_regression.py` 在 output/StorageModuleB3 的两个隔离源码快照中执行原测试，不修改旧断言。历史失败会保留。

`audit_scope.py` 是针对本轮固定基线的范围审计，不是跨版本通用 CI。完整模拟轨迹、固件和旧输出备份仅放 output；仓库保留精简结果、构建日志和报告。

实板项目见 verification_report.md；软件模拟通过不代表实板通过。
