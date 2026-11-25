# M3 全量软件回归

| 模式 | O0 | O2 | 合计断言 | FAIL |
|---|---:|---:|---:|---:|
| 默认监控及 UI1 | 69 PASS | 69 PASS | 60480 | 0 |
| UI1 至候选全量菜单 | 89 PASS | 89 PASS | 459022 | 0 |
| 用例 81（含于全量） | 6000 帧 PASS | 6000 帧 PASS | 396016 | 0 |
| 聚焦用例 88（候选） | PASS | PASS | 62 | 0 |

全量长稳用例跨 Tick 回绕，每个优化档验证 6000 帧逐页发送与旧帧保持，单档 198008 断言。宿主耗时约 O0 45.01 s、O2 35.63 s，仅作本机仿真记录，不能推断板卡帧率。全部用例的断言数、优化设置和返回状态见 [默认 JSON](default-results.json)、[综合 JSON](full-results.json)；控制台见 [默认](default-regression.log.txt)、[综合](full-regression.log.txt)。

**失败清单**：最终无 FAIL。基线聚焦用例在 O0/O2 均于第 12 行失败，整改后同断言通过。证据见 [修复前](before-fix.log.txt)、[修复后](after-fix.log.txt)。未知用例选择会返回非零，不能得到空成功。

复现（Python 安装 Unicorn 2.1.4 与 pyelftools 0.33，准备 ARMCC 5.06u7 build 960）：

```powershell
$env:OLED_ARMCC = 'D:\32\ARM\ARMCC\Bin\armcc.exe'
python Tests/OledMonitor/run_tests.py
python Tests/OledMonitor/run_tests.py --ui2
python Tests/OledMonitor/run_tests.py --ui2 --case 88
```

可省略环境变量以使用仓库已有本机默认路径。软件用假 BSP 注入传输完成及故障，不等同实板。回归覆盖 UI1 图元，UI2 状态机，UI3 队列，UI4 动画与弹窗，UI5 快照，50 ms 调度，刷新期间所有权、回滚、System Monitor 和原四行。CM4/CM7 构建单列见 [M2](build_verification.md)。
