# ApplicationV3 合并审核

**READY_FOR_MERGE_REVIEW**

| 项目 | 记录 |
| --- | --- |
| 开发分支 | `codex/application-v3-media-usb` |
| 经构建和回归核验的源码候选 | `eb619f6cbb9c2a8efd6d2c1efd684b855238c6c3` |
| 清理前基线 | `6d4cc7cf8894e2f491577e3bb612a4e10bf8094c` |
| 冻结 main | `f0ce45fd9ab7250b1e22260e087d606ee5ed9ecb` |

源码候选以冻结 main 为祖先。合并预检执行 `git merge-tree --write-tree f0ce45f eb619f6`，结果与源码候选树一致。审核文档单独归档，交付 SHA 以开发分支上的归档提交为准。

- 已验证正式 JPEG、Raw、CM4 与独立联合测试构建。两个正式 CM7 Map 通过保留符号及测试符号缺失检查。
- ARMCC5 O0/O2 主回归通过。旧 FIFO 白盒三个历史失败保持原断言和原结果。
- USB/SD 联合实板延期到实际使用时进行，状态为 NOT_RUN。最终审核与 main 合并由用户决定。

构建、日志、Map、AXF、HEX、对照结果和本地备份位于 `output/ApplicationV3/PreMerge-6d4cc7c/`。其中 `candidate/` 是经验证的正式源码快照。当前工程目录保留原 Keil 设置和本地 A4.1 文件，审核以 Git 提交树或该快照为准。用户的 16 个本地文件已保存字节校验及完整差异。

[清理清单](cleanup_inventory.md) · [架构审核](architecture_audit.md) · [接口清单](api_changes.md) · [构建核验](build_verification.md) · [软件回归](regression_report.md) · [已知限制](known_limitations.md)
