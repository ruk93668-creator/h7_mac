# 软件回归

以下计数分别适用于 ARMCC5 O0 与 O2。软件结果来自 Cortex-M7 仿真。Media、Dispatcher、UVC Reader 与独立 StorageFS 回归使用正式候选源码。

| 配置 | O0 | O2 |
| --- | ---: | ---: |
| 产品 Runtime/Service 与拒绝旧 Mock 请求 | 62/62 | 62/62 |
| 历史 Startup，含 Mock Mailbox/Registry/PubSub | 73/73 | 73/73 |
| MediaLease，含 JPEG 显式消费者 | 35/35 | 35/35 |
| Dispatcher | 2/2 | 2/2 |
| UVC Reader | 11/11 | 11/11 |
| 产品 Raw / 历史 Raw 各自 | 8/8 | 8/8 |
| JPEG USB/SD 联合 | 6/6 | 6/6 |
| RecordBoard | 2/2 | 2/2 |
| StorageFS + FatFs + AVI | 2/2 | 2/2 |
| AVI 文件/索引上限 | 2/2 | 2/2 |
| USB 平台事件诊断 | 2/2 | 2/2 |

Camera Smoke/Repeat 为 5/2 项。USB Smoke/Repeat/Recovery 为 12/11/10 项。Storage Smoke/Repeat/Recovery 为 5/2/3 项。每组在 O0/O2 均全部 PASS。

Storage 专项连接真实 StorageFS、FatFs、diskio 与 AVI 编写器，使用 RAM 扇区模拟第三分区。覆盖只读禁止写入、已存在文件防覆盖、关闭后旧句柄失效、RIFF/movi/idx1 回读、同步故障保留 SD 独占以及显式恢复。独立 AVI 边界夹具验证 256 MiB 精确收尾及 65,536 索引限制。接口和 ABI 检查保留 52/16/80 B。

清理首轮联合测试暴露基线测试桩缺少 `raw_completed_frames`，干净树不可编译。现在测试桩用已定义的 USB 完成计数字段模拟诊断，原 6 项全部重跑通过。原始日志保留在 `Regression/Joint.log`，复跑见 `FinalRegression`。新增 StorageFS 夹具的首轮链接失败及修正后的通过记录也保留。

## 历史 FIFO 白盒失败

R1 与清理版在 O0/O2 各 13 PASS、3 FAIL，原断言全部保留。`Regression/FifoHistoryFinal/results.json` 保存前后实际值。

| 编号 | R1 与清理版观察 | 影响 |
| --- | --- | --- |
| 5 | 原 72 块合成流在检查点没有产生跨尾 READY，`wrapped` 断言失败 | 旧夹具未覆盖它要断言的输入。MediaLease 33 直接把一个 JPEG 摆在边界，验证两 Span 的字节内容 |
| 7 | READY 实测 6，旧断言要求 7 | `Mjpeg_Publish` 已按 `MAX_LOANS - 1` 预留余量，旧断言只看声明容量 |
| 8 | 原断言期望块 0 剩 6 个引用，实测 1 | `Mjpeg_TakeLatest` 会先淘汰积压的旧 READY，引用减少不局限于刚归还的帧。逐块账本检查与实际策略一致 |

三个项目的失败点、观测值及检查数在前后保持一致。旧测试的这些场景不能替代实际 DCMI DMA 边界或系统长时间压力验收。
