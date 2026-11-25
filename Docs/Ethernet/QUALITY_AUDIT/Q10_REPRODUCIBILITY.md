# Q10.1 Ethernet 候选可复现性

候选基线为 `67a4271dcb41e4c3d4075a23e786400ed0352cd5`。候选身份由基线、覆盖包和逐文件 SHA-256 共同确定。覆盖包是供审核的本地证据，未形成 Git 提交。

| 分组 | 处理 |
| --- | --- |
| A 正式工程整理 | 12 个路径，包含 CM7 Keil/Scatter、HTTP/MJPEG 迁移及 lwIP 断言与时基。包括 4 个旧路径删除 |
| B 用户 RTE | 仅原工作区留存。候选使用 Ethernet 基线的 RTE，整合副本使用 main 的 RTE |
| C 测试及文档 | 与 A 分列保存，构建使用既有的测试入口、独立工程生成脚本和共享源文件清单 |

先导出基线树，再将覆盖包按原路径解压，并只删除清单 `deleted` 指定的路径。对照逐文件 SHA-256 验证后，以该树执行全量重编译。使用主机 MSVC 的 x64 开发环境运行测试。Keil 会在临时副本写入 RTE，完成每次编译后恢复副本原始内容。快照和哈希均覆盖本轮开始时的可见 Git 文件及未跟踪候选内容，忽略的旧构建产物不作输入。最终哈希核对排除本轮新增证据目录。

- [开始状态、A/B/C 清单与 RTE 基线](Q10_Evidence/snapshot.json)
- [完整工作树 Diff](Q10_Evidence/workspace.diff) · [暂存区 Diff](Q10_Evidence/staged.diff) · [未跟踪清单（NUL 分隔）](Q10_Evidence/untracked.zlist)
- [原工作区逐文件 SHA-256](Q10_Evidence/original-sha256.json) · [候选 SHA-256](Q10_Evidence/candidate-sha256.json)
- [覆盖清单](Q10_Evidence/candidate-overlay.json) · [覆盖包](Q10_Evidence/candidate-overlay.zip) · [候选 Keil XML](Q10_Evidence/candidate-keil.xml)

Q9 对比以目标结果、Code/RO/RW/ZI 和 MAP 为准。AXF 带调试信息，采用实际各次文件 SHA-256 记录。可复现判定限定为源文件树与构建验证结果，未要求跨路径生成逐字节相同的 AXF。两套结果与每目标哈希见 [Q10 最终报告](Q10_FINAL_REPORT.md) 和 [机器可读结果](Q10_Evidence/provenance.json)。

使用 ARMCC fromelf --bincombined 对 Q9 和 Q10 的正式 CM7、CM4 及全部 9 个独立目标提取 Flash 装载数据。11 个目标的字节数和 SHA-256 全部相同。AXF 容器哈希仍单独记录。未复制或烧录 Flash 二进制。

[Flash 装载数据对照](Q10_Evidence/candidate-flash-payload-comparison.json)
