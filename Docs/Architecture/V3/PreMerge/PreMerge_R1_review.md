# PreMerge-R1 最终复审

**READY_FOR_MERGE_REVIEW**

经验证的正式源码提交为 `ff9dc967d2be254d60a4ef05772fd8a3942eef6d`。整改基线为 `3f8a573478b9dcfc8fda39f4b26bbc36a48164d9`。开发分支的审核文档提交包含本报告及最终源码。冻结 main 保持 `f0ce45fd9ab7250b1e22260e087d606ee5ed9ecb`。

## 验证结果

- 原工程源码构建的 CM7 正式 JPEG、CM7 正式 Raw、CM4，以及隔离树中的联合测试完整 Rebuild 均为 0 Error、0 Warning。应用命令 ABI 52 / 16 / 80 B 保持，CM4 HEX 逐字节匹配冻结基线。
- 独立的 ARMCC5 O0/O2 回归全部通过。最终提交复测 MediaLease 各 35/35、联合专项各 6/6、正式 Runtime/Service 各 62/62。原 FIFO 白盒各 13 PASS、3 个历史 FAIL，编号 5、7、8 的断言及观测值保持一致。
- 每种正式 CM7 配置均对照了 92 个对象的可执行段。变化只出现在三个函数的五处断言行号加载中，每种配置共 11 个指令字节变化。相同断言表达式在新旧源码均得到核对。节大小与 Code/RO/RW/ZI 保持一致。正式 Map 中测试专用符号为零。
- 原工程当前 CM7 下载产物已同步为已验证的正式 JPEG AXF 与 Map，原联合测试产物已单独备份。全部实板延期项保持 NOT_RUN。

原始证据在 `output/ApplicationV3/PreMerge-R1-3f8a573/`，包括 `KeilFinal/verification.json`、`KeilTestFinal/verification.json`、`machine-review.json`、`Regression/exit.json`、`FifoHistory/results.json`、`final-spotchecks.json` 和完整 `PreMerge-R1.diff`。

[注释规范](comment_style_audit.md) · [生产逐文件检查](production_source_audit.md) · [原工程一致性](working_tree_consistency.md)
