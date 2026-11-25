# M0 范围与基线

- 仓库 `ruk93668-creator/h7_mac`，独立分支 `codex/cm4-oled-ui`。2026-11-23 读取 `git status`、跟踪关系、工作树、最近 15 个提交和相对 main 差异，并成功 fetch。GitHub 确认当时 `origin/main=f0ce45fd9ab7250b1e22260e087d606ee5ed9ecb`、UI 远端 `57eb588fb211b7598a0094d256613f6030c934ef`。
- 本地已有两个经归档但未推送的提交 `d212433`、`637d38d`，保留并作为审计输入。冻结生产候选以 [合并审阅入口](merge_readiness.md) 的 SHA 为准。
- 正式工作树 `D:\32\STM32H7\h7_mac_oled_ui`。既有本地 Keil 改动为 `MDK-ARM/h7_mac.uvprojx`、`MDK-ARM/h7_mac.uvoptx`，未参与候选。构建使用已提交配置的独立副本。
- 本阶段范围仅是现有功能冻结、必要缺陷修复、审计、构建、回归和证据封存。保留既有提交与验收记录。主线合并是用户独立决定。

相对 main 的生产文件仅以下 18 项。CM4 Application：`app_oled.c`、`oled_ui.c`、`oled_ui_assets.inc`，以及 `oled_ui_anim`、`oled_ui_compat`、`oled_ui_draw`、`oled_ui_input`、`oled_ui_menu`、`oled_ui_page`、`oled_ui_status` 各自的 `.c/.h`。工程项是 `MDK-ARM/h7_mac.uvprojx`，只新增 CM4 Application 源文件登记。其余为 `Tests/OledMonitor` 与文档及证据。

已知功能缺口不能通过本阶段的整理变成 PASS。参见 [功能矩阵](feature_matrix.md) 与 [剩余风险](known_issues.md)。
