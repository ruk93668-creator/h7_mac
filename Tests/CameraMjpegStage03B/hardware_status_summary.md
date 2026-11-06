# MJPEG实板状态汇总

记录日期：2026-11-06。依据：用户在当前会话明确反馈的实板结果。不是由代理执行实板测试，未推断具体测试镜像SHA、编译Warning数量或恢复测试覆盖范围。

| 版本 | 配置 | 用户确认的最终状态 |
|---|---|---|
| 旧MJPEG | 160×120，约16 FPS | 实板通过 |
| Continuous FIFO | 512 KiB SDRAM FIFO、零整帧复制 | 实板通过 |
| QVGA MJPEG | 320×240，目标25 FPS | 实板通过 |
| VGA MJPEG | 640×480，目标10 FPS，最大70000B | 实板功能通过，帧率未达目标 |

QVGA、Continuous FIFO未提供新的实测FPS；不能把QVGA目标25FPS改写为实测25FPS。VGA已明确未达10FPS，但具体实测数值尚未提供。

旧160×120 Snapshot循环的约16FPS与新Continuous FIFO分开记录，不移用旧版本的实测数值。

本次确认解决了“新FIFO/QVGA/VGA尚无任何实板结果”的状态缺口。仍待记录VGA的Camera、USB、主机分别实测FPS，以及大帧分布、长时间稳定性和各项恢复场景的具体结果。不能从“实板通过”自动推断所有故障注入和恢复用例均通过。

本轮只更新验收记录，不修改生产代码，不提高帧率，不合并main。机器可读记录见同目录`hardware_status_summary.json`。
