# C2.2A / R2 归档验证

测试基线为 `6e36b01`。归档前后均从同一生产树创建隔离快照，使用同一 ARMCC5、Unicorn 和 Keil 配置副本。原工作区的 Keil 工程、用户选项和已有输出均未被用作写入目标。

| 专项 | O0 | O2 | 合计 |
|---|---:|---:|---:|
| C2.2A 核心 67 + Camera 适配 14 | 81/81 | 81/81 | 162/162 |
| R2 STOP 25 + Decision 12 | 37/37 | 37/37 | 74/74 |

主专项的两份新鲜 JSON 报告完整相同，包括组别、优化等级、用例 ID、执行顺序、断言结果和退出码。各归档 C 文件反向还原引用路径后逐字节等于归档前文件，测试主体和原断言保持原样。

## R2 附加入口

| 入口 | 归档前 / 后退出码 | 结论 |
|---|---|---|
| `run_original.py` | 1 / 1 | 当前 P8.2 源码下，旧适配链接均缺 `V2Runtime_Round`。在固定历史版本 `f2da385` 上独立复现 162/162，退出码 0。 |
| `run_protocol.py` | 0 / 0 | 在隔离副本放置既有 Unicorn/ELF 依赖后，两边 C1 419/419、C1.1 24/24、C2.1.1 648/648，ABI 检查通过，门禁自测 29 项。首轮未放置依赖时的错误日志亦保留。 |
| `run_regression.py` | 1 / 1 | 缩短快照路径后，28 个套件的 ID、结果、门禁判定逐项一致。两边各有 8 个失败套件，原比较器及退出语义保持。 |
| `run_build.py` | 0 / 0 | 两个快照均完成 CM7/CM4 Rebuild，均 0 Error、0 Warning。CM7 可加载段和 CM4 HEX 逐字节一致。 |
| `run_layout.py` | 1 / 1 | 当前布局夹具的链接同样缺 `V2Runtime_Round`，原布局断言没有修改。 |
| `audit_scope.py` | 1 / 1 | 仍对照 `fa9ce5d` 的旧生产改动白名单。P8.2 基线已超出该范围，旧门禁保持不变。本轮独立核对生产和工程的 Git 暂存差异为零。 |

完整回归第一次在更长的快照路径下运行。归档侧 ARMCC5 无法打开仍存在的深层 `endpoint_lifecycle_test.c`，导致报告差异。使用同一证据根下的短路径 `b`、`c` 重跑后判定一致。原有 8 个失败套件为 B1Behavior、B1BridgeStorage、B1Media、CameraApp、CameraDeviceLifecycle、JpegDevice、NativeIntegrated、NativeTest。当前历史回归夹具仍有混合旧源码快照所致的失败，例如复制后的资格头找不到共享代次头。这些既有依赖和断言超出此次路径迁移范围。

## B1 历史失败与证据位置

在固定组合版本 `f2da385` 的独立快照实际执行 B1。结果仍为 **37/39**，只失败 ID9、ID10，原脚本退出码为 **1**。归档的 `FinalCombination20261114` 历史 JSON、日志和原始轨迹均保持原 SHA256。

所有新增验证文件均位于 `output/LegacyLifecycleArchiveValidation/`。主专项逐项对照和固件摘要见 `v155618/assertion_and_case_comparison.json`、`v155618/firmware_comparison.json`。协议复跑见 `v155953`。短路径回归汇总见 `v160657/regression_parity.json`，实际脚本输出见同一根目录下的 `b`、`c` 两个快照。B1 固定提交复跑见 `B1_f2da385/verified.json`。
