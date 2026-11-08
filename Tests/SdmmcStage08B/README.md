# SDMMC Stage08B

当前临时恢复旧C组d030531复测16FPS：BLOCK=1、STRESS=1、WRITE=0，固定20ms，不含D/POLL_ONLY开关或可调周期。只运行旧C对应run_tests.py（556次）与run_policy_checks.py（15项）；D/Poll源码及审计记录保留作历史，需在36ea40b/6bc08f8对应版本执行。先不处理M-V累积。

- [设计、接口及实板验收](../../Docs/Design/SDMMC1_Stage08B.md)
- [验收报告](verification_report.md)
- 实际BSP/HAL与原08A断言：`python Tests/SdmmcStage08B/run_tests.py`
- 写测试编译保护：`python Tests/SdmmcStage08B/run_policy_checks.py`
- 历史回归（保留已知失败）：`python Tests/SdmmcStage08B/run_regressions.py`
- 失败专项基线对照：`python Tests/SdmmcStage08B/compare_baseline.py`
- 同一正式工程三种配置全量构建：`python Tests/SdmmcStage08B/run_profiles.py`
- 当前本机配置两核全量构建：`python Tests/SdmmcStage08B/run_build.py`

脚本不创建新的Keil工程。profile构建临时替换测试宏，finally恢复原项目字节。硬件写测试默认关闭，编译用合成CID不代表真实卡授权。output/和tmp/保留但不提交。

## 不兼容测试清理

用户已要求移除确认过时的测试入口。当前集合及实际结果见active_regression_results.json；原regression_results.json、baseline_comparison.json和verification_report.md保留13364dc时的历史验收语义，不能当作当前覆盖数量。
清理清单见../TestMaintenance/incompatible_tests_removed.json。共享桩保留，未修改生产代码，也未降低保留用例的断言。

清理前的完整逐例证据以Git提交13364dc为准；本轮详情使用active_前缀保存，未回写恢复当前已有日志文件。

## 五分钟只读压力

[入口、Watch与实板验收](readonly_stress_report.md)。request=5，默认关闭，复用SdTest任务，与写测试互斥。原测试全部保留，新增O0/O2压力测试与编译保护。

当前08B分支按用户要求直接在h7_mac_CM7启用只读压力配置。打开原h7_mac.uvprojx，编译、Download即可；额外profile镜像已删除，旧报告中的专用镜像路径不再适用。完成验收后关闭测试宏。

最新：压力验收后已按用户要求关闭CM7全部SD测试宏，用于同视频配置下的USB FPS对照复测。无需设置request，旧启用说明只作历史记录。

当前为D组：BLOCK=1、STRESS=1、WRITE=0、APP_SD_BLOCK_D_GROUP_ENABLED=1；仅保留链接，不创建SdTest，不能执行request压力。D组链接审计：python Tests/SdmmcStage08B/run_d_group_audit.py。烧录后先观察USB FPS，不读写SD卡。

最新轮询对照：D_GROUP=0、POLL_ONLY=1，SdTest恢复创建；Watch修改g_sd_block_test.poll_period_ms，默认20，可改1000再改20。所有SD请求被拒绝，不需要设置request。链接审计使用run_d_group_audit.py --poll-only。
