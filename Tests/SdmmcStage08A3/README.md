# SDMMC1 / Stage08A-3

基线main@cbc4c41，默认关闭的低速1-bit识卡验收。

- [完整软件报告](verification_report.md)
- [实板步骤与Watch](../../Docs/Design/SDMMC1_Stage08A3.md)
- 专项：`python Tests/SdmmcStage08A3/run_tests.py`
- 原回归：`python Tests/SdmmcStage08A3/run_regressions.py`
- 检测/识卡镜像构建：`python Tests/SdmmcStage08A3/run_probe_builds.py`
- 默认两核全量构建：`python Tests/SdmmcStage08A3/run_build.py`

所有入口使用同一个正式Keil工程，无新增临时uvprojx。探针构建临时附加宏，finally按字节还原工程；最后执行默认构建避免误烧测试镜像。
需要本机ARMCC5、UV4、Unicorn与pyelftools。测试编译实际BSP，HAL被Mock；没有真实卡验收。
