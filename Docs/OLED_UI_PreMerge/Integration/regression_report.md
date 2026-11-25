# 软件回归

OLED 通过 ARMCC 5 构建，以 Cortex-M 执行 O0 和 O2。默认用例从真实首页启动，综合用例只在测试夹具投递事件。两套使用相同生产代码和调度。

| OLED 套件 | 通过 | 断言 |
| --- | ---: | ---: |
| OLED 默认 | 138/138 | 60490 |
| OLED 综合 | 178/178 | 459050 |

覆盖灰阶无关的一位像素裁剪、原监控、回滚、四页异步所有权、LSE、编码器、输入隔离、分段动画、Tiles、编辑与窗口。运行命令为 `python Tests/OledMonitor/run_tests.py` 与 `python Tests/OledMonitor/run_tests.py --ui2`。

ApplicationV3 结果保存在 [Evidence](Evidence/)。板测 profile 由 `Tests/ApplicationV3/BoardProfiles/prepare.py` 在独立副本生成。

| ApplicationV3 套件 | 通过 | 执行说明 |
| --- | ---: | --- |
| IntegrationV3-camera-off | 10/10 | 见结果各条优化档 |
| IntegrationV3-camera-repeat | 4/4 | 见结果各条优化档 |
| IntegrationV3-FrameDispatcher | 4/4 | 见结果各条优化档 |
| IntegrationV3-joint | 12/12 | 见结果各条优化档 |
| IntegrationV3-MediaLease | 70/70 | 见结果各条优化档 |
| IntegrationV3-storage-off | 10/10 | 见结果各条优化档 |
| IntegrationV3-storage-recovery | 6/6 | 见结果各条优化档 |
| IntegrationV3-storage-repeat | 4/4 | 见结果各条优化档 |
| IntegrationV3-StorageFs | 4/4 | 见结果各条优化档 |
| IntegrationV3-usb-off | 24/24 | 见结果各条优化档 |
| IntegrationV3-usb-recovery | 20/20 | 见结果各条优化档 |
| IntegrationV3-usb-repeat | 22/22 | 见结果各条优化档 |
| IntegrationV3-UvcReader | 22/22 | O0 与 O2 各运行 11 项 |
| IntegrationV3JPEG | 124/124 | 见结果各条优化档 |
| IntegrationV3Raw | 16/16 | 见结果各条优化档 |
