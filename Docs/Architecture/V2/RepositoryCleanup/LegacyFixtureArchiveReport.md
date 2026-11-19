# C2.2A / R2 历史测试夹具归档

归档基线 `6e36b01f34cdc5555db6ce368f20f7986df2fabd`。开始前本地和 GitHub 清理分支均为此提交。两个原测试目录内没有未提交文件。

| 原路径 | 归档路径 | 文件数 |
|---|---|---:|
| `Tests/ConsumerQualificationC2_2A` | `Tests/LegacyLifecycle/C2.2A` | 19 |
| `Tests/CameraStopOwnershipR2` | `Tests/LegacyLifecycle/R2` | 40 |

`git mv` 阶段确认 59 个 R100。最终 39 个文件逐字节相同，20 个文件仅改变路径或输出定位。所有历史 JSON、构建日志与 `FinalCombination20261114` 文件逐字节一致。逐文件原始及归档 SHA256 见 [归档清单](LegacyFixtureArchiveManifest.csv)。

## 路径调整

- C2.2A 的 `adapter_test.c`、`layout_test.c` 和 R2 的 `layout_test.c` 改用上两级的 `CameraModuleB1/behavior_test.c`。C2.2A 的 `qualification_test.c` 继续通过相对路径编译原产品资格实现，R2 的 `decision_test.c` 改用相邻的 `C2.2A/qualification_test.c`。
- Python 入口更新到仓库根目录的层级。C1 测试框架仍取自 `Tests/CameraLifecycleC1`。R2 的 `run_original.py` 改读归档后的 C2.2A 文件。范围审计保留 `git show` 使用的旧 Git 树路径，用新路径读取当前夹具。
- 运行器把新生成报告和中间产物放入 `output/LegacyLifecycleArchiveValidation/C2.2A` 或 `R2`。验证在新输出树下的独立仓库副本运行，Keil 工作文件和子脚本输出也位于该树。运行正式构建入口时应使用隔离副本。
- 更新归档 README 和仓库文档中指向夹具的链接与执行命令。历史报告中的 Git 版本路径和当时的输出路径属于证据，保留原记录。

逐字节回退三个 C include 路径替换后，6 个 C 文件都与原文件相同。Python 差异仅涉及文件及报告路径。历史验收报告内 B1 ID9、ID10、原退出码和失败轨迹完整保留。测试、构建和附加门禁结果见 [验证报告](LegacyFixtureValidationReport.md)。

本次 Git 差异限于测试夹具与文档。产品代码、ABI、Replay、generation、Keil Target 和任务配置均无提交差异。用户在工作区的两个 Keil 文件与现有 output 已保留。本次归档不批准剥离产品资格接口。
