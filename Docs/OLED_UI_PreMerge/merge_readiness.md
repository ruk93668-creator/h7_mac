# M5 合并审阅入口

**READY_FOR_MERGE_REVIEW**。这是可复核的生产候选和证据，供独立审核。实际合并、完整 OLED_UI 等价、实板 20 FPS 和真实 CM7 诊断各有自己的门槛。

- 冻结生产候选 `b4044beec36476b1f327d961bca27d795ffeba90`。专门的资料提交封存构建和回归证据，并增强测试脚本的路径可配置性及非法用例参数检查，生产树不再变化。
- 核对基线 `origin/main=f0ce45fd9ab7250b1e22260e087d606ee5ed9ecb`；未执行任何正式合并。独立支线 `codex/cm4-oled-ui`。
- 软件终态含 138 默认、178 综合 PASS，CM4 正式、Debug、CM7 完整重建 0 错误、0 警告。候选输入隔离缺陷已修复。实际板测全为 NOT_RUN。

| 审阅 | 证据 |
|---|---|
| M0 范围和功能冻结 | [scope.md](scope.md)、[feature_matrix.md](feature_matrix.md) |
| M1 架构和缺陷整改 | [architecture_audit.md](architecture_audit.md)、[code_review.md](code_review.md) |
| M2 工程与固件 | [build_verification.md](build_verification.md) |
| M3 可复现回归 | [regression_results.md](regression_results.md) |
| M4 主线兼容与风险 | [main_compatibility.md](main_compatibility.md)、[known_issues.md](known_issues.md) |

正式合并前：

1. 由用户决定是否接受候选尚存的视觉差异和 N/A 范围。已知功能差距保留；生产诊断若纳入合并要求，先审批 CM7/IPC 通路。
2. 以届时真实远端 main 重做合并树、配置对比、引脚/LSE 核对与相关目标构建。特别检查其他支线先合并后对 Keil XML 的影响。
3. 在同一候选 HEX 上验收原四行、U7、列表和 Tiles、回退、编辑器、IPC 失联恢复和稳定性；实测平均完成帧率、帧缓冲所有权及栈峰值。20 FPS 的量测方法见 [性能专题](../UI36/oled-ui-compat-verification.md)。确认通过或单独批准范围例外后再进行合并决策。

在此处停止合并流程，等待 GPT 独立审核。用户保留 main 合并权。
