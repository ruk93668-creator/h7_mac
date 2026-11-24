# 原工作目录一致性

原工程位于 `D:/32/STM32H7/h7_mac`。起点为 `3f8a573`，暂存区为空。备份先于工作区同步。备份保存原文件、未跟踪板测头、工作区状态、相对清理候选的差异、相对板测基线的差异及逐文件 SHA256。

| 位置 | 内容 |
| --- | --- |
| `output/ApplicationV3/PreMerge-R1-3f8a573/LocalProfiles/files/` | 本地未提交原始文件，含 Keil 配置与 A4.1 |
| `LocalProfiles/sha256.json` 与两个 `working-*.diff` | 完整指纹及差异，可独立核对 |
| `LocalProfiles/A4.1/` | 从 `6d4cc7c` 重组的私人板测树，维持原本地 Keil 配置 |
| `test_tree/` | 独立联合测试构建树，覆盖层未应用到原工程 |
| `KeilFinal/` | 直接读取原 `CM7`、`CM4` 与 `Common` 正式源码的构建和 Map |

正式 `h7_mac.uvprojx` 的默认目标保持 JPEG，移除本地单项压力板测宏和测试分区选择。保留用户在 CM4 已禁用的 CM7 Platform 分组里登记的 `platform_sync.c` 浏览条目，该项不会进入 CM4 链接。原 `h7_mac.uvoptx` 保持字节一致。

正式候选提交后核对 `git diff --exit-code HEAD -- CM7 CM4 Common MDK-ARM/h7_mac.uvprojx` 为空。原工程打开的文件与审阅提交一致。为验证 Raw 可用性，验证配置只改变视频格式选择及构建输出位置。实板调试及烧录未执行。
