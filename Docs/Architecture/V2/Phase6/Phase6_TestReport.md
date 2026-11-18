# Phase 6 软件测试与差异

## 本轮执行

| 套件 | O0 | O2 |
| --- | ---: | ---: |
| Storage Joint | 24/24 | 24/24 |
| 真实 Backend 软件层与虚拟 FAT16 卡 | 3/3 | 3/3 |
| Storage 负对照 | 3/3 检出 | 3/3 检出 |

联合测试每个 ID 使用真实 Composition、依赖计划、Storage Lifecycle/Endpoint、Phase 2 MR、Phase 4 Camera 退出查询和现有 JPEG/AVI。物理帧、底层 IO、运输与资格应答使用夹具。 Backend 层专项执行仓库原 StorageFS、FatFS、DiskIO 和 SD Block，再下一层用内存扇区和最小 RTOS stub。两个闭环均未调用真实 SDMMC HAL 或 ISR。

| ID | 场景 |
| --- | --- |
| 00–04 | AVI、双 Span JPG、运输满及 USB 双持有、ACQUIRE BUSY、Mount 失败回滚 |
| 05–09 | 未提交撤回、已提交未准入撤回、读回内容错误、Finish 失败、Close 失败 |
| 10–14 | Media Release IO 保留与重试、旧资格/槽/收据及 Unmount IO、消费队列满、IDMA 故障显式恢复、恢复失败保留物理持有 |
| 15–19 | 写入 IO 不重写残片、缺少 RELEASE 证明、CREATE_NEW 拒绝旧文件、STOP 排空延迟 Offer、Return 满重试 |
| 20–23 | 资格回复延迟、本地证据与实际释放分离、损坏 JPEG、超时后真实排空、退出后新代次重启 |

ID20 是资格回复延迟及局部证据，ID21 是损坏 JPEG，ID22 是超时后真实排空，ID23 是重启。Backend 原生软件层 ID0/1 重新挂载读取 AVI/JPEG，ID2 检查 SD Block/StorageFS 故障后的静止与异常卷恢复。O0/O2 负对照分别验证跳过 DMA 门禁、凭缺失的资格标志退场、伪造文件 Finish，均命中原测试断言。

初次联合运行 ID12 两次失败。原因是收据消费队列 NO_RESOURCE 被当成新收据携带的业务错误。按进入 CONSUME 的状态转换识别真实终结结果，已在 CONSUME 中的临时队列满继续等待。原断言保留，初次失败留存。

## 原有回归

| 项目 | 本轮结果 |
| --- | ---: |
| Phase 1 / 2 / 3 / 4 | 250/250 · 112/112 · 158/158 · 154/154 |
| Phase 4 负对照 | 10/10 检出 |
| Phase 5 / 负对照 | 38/38 · 6/6 检出 |
| Replay / 布局 | 648/648 · 2/2 |
| C1 / C1.1 | 419/419 · 24/24 |

使用原断言，只将新旧 runner 的输出隔离，并保留各原阶段源范围。命令为 `python Tests/V2Phase6/run_tests.py`、`python Tests/V2Phase6/run_regression.py`。原始事件、命令及中间失败保存在 `output/V2Phase6`。Git 中的 `Tests/V2Phase6/Evidence` 保存本轮可核验结果和摘要。
