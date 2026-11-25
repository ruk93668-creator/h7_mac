# Q10 最终报告

| 验收项 | 判定 |
| --- | --- |
| Q10.1 候选可复现性 | REPRODUCIBLE |
| Q10.2 固定 main SHA 兼容性 | MAIN_COMPATIBLE |
| 正式网络和视频启用 | FEATURE_ENABLEMENT_BLOCKED |
| 人工最终审核 | READY_FOR_FINAL_REVIEW |

最终审核范围是本地证据对应的候选和临时整合树，且正式网络和视频默认禁用。没有验证实板或批准功能上线。原分支仍为 67a4271，main 对照点为 851c111。

## 固件全量编译

单元格依次为状态、Error/Warning、Code / RO / RW / ZI（B）。

| 目标 | Ethernet 候选 | 临时整合 |
| --- | --- | --- |
| cm7 | PASS · 0/0 · 200778 / 4894 / 1636 / 156716 | PASS · 0/0 · 203434 / 5702 / 1592 / 156744 |
| cm4 | PASS · 0/0 · 26302 / 1438 / 936 / 3576 | PASS · 0/0 · 41578 / 20474 / 1040 / 3712 |
| e4 | PASS · 0/0 · 26842 / 1110 / 156 / 38388 | PASS · 0/0 · 26842 / 1110 / 156 / 38388 |
| e42 | PASS · 0/0 · 38030 / 1242 / 212 / 38720 | PASS · 0/0 · 38030 / 1242 / 212 / 38720 |
| e5 | PASS · 0/0 · 67546 / 1614 / 348 / 68588 | PASS · 0/0 · 67546 / 1614 / 348 / 68588 |
| e6 | PASS · 0/0 · 70262 / 51434 / 332 / 64380 | PASS · 0/0 · 70262 / 51434 / 332 / 64380 |
| e7 | PASS · 0/0 · 109938 / 4154 / 516 / 127968 | PASS · 0/0 · 112278 / 4182 / 520 / 128084 |
| e7_v1 | PASS · 0/0 · 111966 / 4154 / 520 / 128052 | PASS · 0/0 · 112278 / 4182 / 520 / 128084 |
| e7_v1_axi | PASS · 0/0 · 112010 / 4154 / 520 / 128052 | PASS · 0/0 · 112322 / 4182 / 520 / 128084 |
| e7_v1_qvga | PASS · 0/0 · 112014 / 4154 / 520 / 128052 | PASS · 0/0 · 112326 / 4182 / 520 / 128084 |
| e35 | PASS · 0/0 · 68398 / 2862 / 37096 / 81824 | PASS · 0/0 · 139890 / 3958 / 37248 / 112272 |

## 主机回归

单元格为各独立测试进程退出码，0 为通过。

| 回归 | Ethernet 候选 | 临时整合 |
| --- | --- | --- |
| e4 | 0 | 0 |
| e42 | 0 | 0 |
| e5 | 0 | 0 |
| e6 | 0 | 0 |
| e7 | 0 | 0 |
| e7-qvga | 0 | 0 |

候选与 Q9 的 11 个固件目标 Code/RO/RW/ZI 逐一比较，结果记录在 provenance.json。11 个目标的 Flash 装载数据也与 Q9 逐字节一致（fromelf --bincombined 的 SHA-256 相同）。每个结果可追溯到完整文件树、Keil 与 Scatter SHA-256，以及 AXF 和 MAP 哈希。[机器可读结果](Q10_Evidence/provenance.json) · [候选构建与 MAP](Q10_Evidence/candidate/) · [整合构建与 MAP](Q10_Evidence/integration/)

## 兼容失败与复验证据

整合版首次 `--e7` 有 7 条 L6218E。main 的生产 Camera BSP 已使用 V1 的 HAL I2C，原生成器只对独立 V1 BSP 加入 I2C/I2CEx 源文件。临时解决方式是让 E7 目标随相机路径包含这两个 HAL 文件，调整整合树的相机板型记录，并将 I2C MAP 断言覆盖到 `--e7`。生产 BSP、Application 和 Keil 正式目标保持输入不变。重跑了受条件影响的四个 E7 变体。其余固件与六组主机用例的输入和构建入口不依赖这三处 `e7` 条件，沿用本轮整合树的首次运行结果。整合 Diff 与覆盖包包含临时解决项，Ethernet 候选覆盖包无此项。

[首轮 E7 失败日志](Q10_Evidence/integration-initial-e7/e7_build.log) · [测试生成器兼容差异](Q10_Evidence/integration-e7-generator.diff)

## 正式运行时门禁

| 门禁 | 证据与判定 |
| --- | --- |
| lwIP 唯一上下文 | NO_SYS Raw API 仍只在独立测试的串行循环运行，正式任务所有权和请求通道待设计、验证 |
| Network Stop/Restart | 单例持有 Network/Transport 状态，正式服务停止和重启完成条件及资源回收未验收 |
| Camera Frame Lease | E7 验证私有复制和测试租约；正式消费者跨任务归还及失败隔离门禁未关闭 |
| MPU/DCache | MAP 和 MPU 4 设计已检查，正式 Region 5 属性与 CCR/IRQ 实板读回待验证 |
| 并行实板 | Camera、Ethernet、USB、Storage、CM4 OLED 同时运行的正式实板证据缺失 |

正式程序仅链接入口。对两个版本的 CM7、CM4、Common、Platform 运行时源码搜索未发现启动或 Poll 调用，也未注册网络任务。临时整合保留 main 原有的应用与资源生命周期设置。[候选调用检索](Q10_Evidence/candidate-runtime-xref.txt) · [整合调用检索](Q10_Evidence/integration-runtime-xref.txt)

原工作区的状态、已跟踪差异、索引、未跟踪清单和快照文件内容均通过逐项核对。RTE 原始 SHA-256 前后一致。未提交、推送、改变分支、创建 PR、合并或烧录。
