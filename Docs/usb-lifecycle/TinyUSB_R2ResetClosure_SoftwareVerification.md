# TinyUSB R2-ResetClosure — 软件验证

**VERIFIED（软件模型）**：新增 Reset 专项31项、574断言通过。成功路径执行修改后的真实完整 ARMCC5 固件，包含实际 FreeRTOS queue/mutex/port、TinyUSB DCD/USBD/MSC、原 RAM Backend；未用 Fake DCD 替代 Reset 成功。**NOT_TESTED**：实板、真实调度延迟和物理时钟准确度。

## 可复现命令与固件一致性

工作目录 `D:/32/STM32H7/h7_mac_lifecycle`。按顺序执行：

```text
python Tests/TinyUSBReplacement/build_r2.py
python Tests/TinyUSBReplacement/run_r2_reset_tests.py
python Tests/TinyUSBReplacement/run_r2_firmware_tests.py
python Tests/TinyUSBReplacement/run_r2_closure_tests.py
python Tests/TinyUSBReplacement/run_r1b_tests.py
python Tests/TinyUSBReplacement/run_stack_tests.py
python Tests/TinyUSBReplacement/run_r1b_wait_tests.py
python Tests/TinyUSBReplacement/run_r1_tests.py
python Tests/TinyUSBReplacement/audit_upstream.py
python Tests/TinyUSBReplacement/check_r2_default.py
python Tests/TinyUSBReplacement/collect_r2_reset_evidence.py
```

VERIFIED：上述所有入口实际执行。旧断言失败入口返回非零是保留的验收结果，不能用最后一个命令的退出码表示全部测试通过。收集器读取逐用例结果/哈希，而不以组合 shell 最终退出码作结论。

VERIFIED：完整构建0 Error/0 Warning，AXF SHA256 `28b17dae3c265752db075c2c93b6648d33414c14e6694685cd1d5c6433132c9b`。完整AXF测试会先核对构建 receipt 内每个源文件 SHA，再载入真实 Flash LMA 并执行 ARMCC scatter/decompress 初始化，避免压缩 RW 被模型虚构。MAP、HEX、AXF、日志在 `D:\32\STM32H7\h7_mac_lifecycle\output\TinyUSBReplacement\R2ResetClosure-review-28b17dae3c26`。

## 模型边界

- VERIFIED：ResetModel 不在 USBRST 上清 EPENA、RXFLVL、BUSY、旧指针或 Class；端点仅在软件实际请求 EPDIS 后产生 EPDISD/清 EPENA。可注入缺失 IN/OUT Disable。
- VERIFIED：只有显式写入 RXFFLSH 且硬件模型返回清零时才丢弃共享FIFO内容。GONAKEFF 来自实际 FIFO marker 被真实DCD pop；没有第二消费者。Flush、NAK和释放均可失败。
- VERIFIED：模型操作的是 MMIO 环境、CPU上下文和物理事件输入，不直接写软件 proof、SUCCESS、Buffer pointer 或 busy 来完成测试。
- VERIFIED：Task/IRQ抢占测试在实际 ARM `usbd_spin_lock` 入口暂停 Task，保存CPU/栈，执行实际 OTG Reset handler，再恢复 Task 执行，验证提交被重新检查并拒绝；不以两个串行独立调用冒充此窗口覆盖。
- INFERRED：这验证了明确抢占点的实际指令行为，不是全状态空间并发证明，也不是实际 FreeRTOS 调度测试。模型的 DWT 步进/RTOS tick 是时钟契约输入，不是板上测量。
- VERIFIED：原19项断言正文逐字保留；仅公共硬件夹具补充 DSTS=FS48，纠正原来在FS PHY模型中默认为高速的寄存器输入。旧25项、R1B/MSC/等待/R1测试源码未修改。

## 新增专项逐项结果

| # | 场景 | 断言数 | 结果 |
| -- | -- | -- | -- |
| 1 | active OUT Reset and INQUIRY | 20 | VERIFIED：PASS |
| 2 | active IN Reset and READ | 21 | VERIFIED：PASS |
| 3 | IN OUT simultaneous Reset | 22 | VERIFIED：PASS |
| 4 | EP0 OUT SETUP receiver remains enabled | 20 | VERIFIED：PASS |
| 5 | EP0 IN and Reset | 20 | VERIFIED：PASS |
| 6 | RX leftovers consumed by explicit flush | 20 | VERIFIED：PASS |
| 7 | new SETUP during pending discarded | 20 | VERIFIED：PASS |
| 8 | missing IN EPDISD | 18 | VERIFIED：PASS |
| 9 | missing OUT EPDISD | 16 | VERIFIED：PASS |
| 10 | missing Global NAK | 16 | VERIFIED：PASS |
| 11 | RX Flush stuck | 17 | VERIFIED：PASS |
| 12 | TX Flush stuck | 16 | VERIFIED：PASS |
| 13 | ENUM before RX proof | 16 | VERIFIED：PASS |
| 14 | repeat Reset pending | 16 | VERIFIED：PASS |
| 15 | Reset group close overlap | 2 | VERIFIED：PASS |
| 16 | Backend pending across Reset | 23 | VERIFIED：PASS |
| 17 | late old events | 21 | VERIFIED：PASS |
| 18 | three Reset reconfigurations | 24 | VERIFIED：PASS |
| 19 | Reset Disconnect overlap | 16 | VERIFIED：PASS |
| 20 | DWT stops during Reset | 16 | VERIFIED：PASS |
| 21 | DWT wraps during Reset | 20 | VERIFIED：PASS |
| 22 | RX first attempt stalls then retry succeeds | 21 | VERIFIED：PASS |
| 23 | RX old CBW pending at reset | 20 | VERIFIED：PASS |
| 24 | old Status queued across reset | 20 | VERIFIED：PASS |
| 25 | old Reset END after new Session | 21 | VERIFIED：PASS |
| 26 | old SETUP DONE without fresh RX | 21 | VERIFIED：PASS |
| 27 | EP0 fresh descriptor after Reset | 21 | VERIFIED：PASS |
| 28 | new SETUP arrives during RX flush | 20 | VERIFIED：PASS |
| 29 | IRQ remains enabled across reset | 20 | VERIFIED：PASS |
| 30 | Backend completion queued during Reset | 22 | VERIFIED：PASS |
| 31 | Reset IRQ before Task transfer publication | 8 | VERIFIED：PASS |

VERIFIED：正常路径在硬件事件、FIFO、NAK proof 后才原生 Class reset；Class 回调观察到 BASEPRI=0。Pending 下 Clear HALT不修改BUSY，Abort不释放也不制造fault。正常 Reset 后新配置、INQUIRY/READ和CBW通过；Backend pending保持至实际完成。

## 完整回归与旧契约分类

| 入口 | 本轮结果 | 对照基线 | 分类 |
| -- | -- | -- | -- |
| Reset 专项 | 31/31，574断言 | 新增 | VERIFIED |
| R1B | 33/33，313断言 | 无用例回退 | VERIFIED |
| 原MSC | 12/12，1679断言 | 无回退；原入口使用Fake DCD做协议验证，与上面的真实DCD测试分开 | VERIFIED |
| 等待边界 | 9/9 expectation_met | 全部有界返回 | VERIFIED |
| R2-Closure原25项 | 24/25，185断言 | 原活动Reset立即fault断言FAIL，其余24项不变 | CONTRACT_CHANGED，原断言保留 |
| 原R2完整AXF19项 | 17/19，114断言 | 活动Reset立即fault新FAIL；活动Disconnect立即fault为既有FAIL；其余不变 | Reset CONTRACT_CHANGED；Disconnect历史契约FAIL保留 |
| 原R1两项 | 0/2，3断言 | 与基线相同：IN夹具unmapped read、OUT旧fail-closed断言不符 | 历史FAIL，未修改 |
| 默认入口/唯一IRQ/上游差异 | 通过收集与检查 | 默认工程未变，唯一向量指向TinyUSB | VERIFIED（源码/AXF） |

VERIFIED：逐用例前后对照在 `Tests/TinyUSBReplacement/reports/R2ResetClosure_caseComparison.json`。两条新 Reset 旧断言差异是本任务明确要求从“立即故障”转为“先Pending、proof后恢复”；不能修改它们使报告全绿。人工审核后再决定如何迁移历史断言，本轮没有执行迁移。新31项承担新契约成功与失败覆盖。

## 原始失败与构建历史

VERIFIED：所有 `reports/*-history-*.json`、`r2-build-r2-firmware-*.log` 和 output 都保留。包含本轮最初未定义 EPDIS mask 宏导致的编译失败，以及新夹具开发中 FIFO计数包含初始化、未链接API调用、静态BUSY预期、测试场景选择错误等原始FAIL。修正后重新完整构建/运行，未删除或覆盖历史。最终结果不能抹去这些开发记录。

VERIFIED：`usbd_edpt_release` 在当前RAM固件因无调用者而被链接器剔除；其Reset门控属源码复核，未宣称直接AXF执行该API。ISO/DMA/其他DCD不在本轮软件覆盖范围。原固定上游52个文件的原始Git blob核验通过，累计差异仍限定原11个文件，本轮生产改动仅其中3个。

## 证据入口

- VERIFIED：`reports/r2-reset.json` 保存每个断言、实际调用、寄存器写、flush观察、首次failure、回调及NVIC记录。
- VERIFIED：`reports/r2-closure.json`、`r2-firmware-tests.json`、`r1b.json`、`stack.json`、`r1b-waits.json`、`r1.json` 分别保存原入口结果。
- VERIFIED：`reports/R2ResetClosure_receipt.json`、`R2ResetClosure_production.patch`、`R2ResetClosure_callers.json` 与 `upstream_audit.json` 关联最终代码/产物。
- NOT_TESTED：USBPcap、板级VID/PID枚举、实际EP0 ZLP勘误、物理Reset/拔插。本轮未烧录。
