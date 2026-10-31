# USB Stage 4 历史记录

本目录保存阶段验收与合并说明，不是CI测试目录。[验收与合并状态](verification_report.md)已更新为正式main提交dbf3e9b完成。

## 日常回归入口

在仓库根目录执行：

```powershell
python Tests/UsbCoreDecoupling/run_tests.py
python Tests/Ep0IrqRace/run_race.py
```

两个测试入口不依赖本目录的历史脚本、快照或补丁。当前执行器仍要求Windows ARMCC5、Unicorn及pyelftools；ARMCC5工具路径写在执行器内，Python依赖需已安装或通过PYTHONPATH配置。它们不是已配置完成的跨平台CI流水线。

测试会重新生成各自目录的commands.json/results.json等结果。204项和4项软件通过不等于实板枚举或UVC视频通过。

## 历史证据

- audit.py、migrate.py、make_report.py原文移至History/*.py.txt，仅供阅读。
- 一次性构建/回归日志、formal_before/protected_sha256等快照、清单和四个分层patch从当前树移除，完整原文仍可从dbf3e9ba798e1434796ecc7d1e585018edb9843c恢复。
- 不依赖其他机器不存在的C:/Users/...或D:/32/...本地路径作为证据入口。

需要原始现场材料时，导出指定历史提交，不要重新运行迁移脚本：

```powershell
git archive --format=zip --output=usb-stage4-history.zip dbf3e9ba798e1434796ecc7d1e585018edb9843c -- Tests/UsbStage4Migration
```

导出ZIP仅用于本地查看。原完整迁移diff可通过`git diff 98a9f89 dbf3e9b`重新取得；无需长期维护重复的分层patch。
