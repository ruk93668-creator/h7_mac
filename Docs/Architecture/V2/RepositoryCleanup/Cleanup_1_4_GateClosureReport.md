# Cleanup 1.4 最终重新验证

清理前 40a72ef51bef17bc19c60b39d7e66a3ec3ec4169。清理后 119c5ef5223d745042a89291864608cd125c2cb3，含 63086c3。原请求中的 119e5cf 无法在本地解析，按实际祖先关系核验。两套源码取自提交，用户工作区中的 V2 注释与工程设置保持原状。原始证据位于 output/q14，上一轮 output/q124 继续保留。

## T1 历史失败复现

B1、B2、B3 的原入口在清理前后均缺 V2Runtime_Round，退出码均为 1。使用相同 ARMCC5、原 runner 参数、相同链接注入及同一历史适配源重跑后，B1 36/39、B2 99/102、B3 49/51。基线和候选的 JSON 报告逐字段相同，八个失败条目中的两侧 C 断言均通过。三套旧 runner 的 ARMCC5 编译、链接实参逐项匹配，只有隔离路径不同。证据见 output/q14/legacy_command_parity.json。八个失败均来自旧 runner 将完整历史轨迹不相等判成 FAIL，失败和退出码保留。

| 用例 | 原测试名称 | 首次差异记录 | 历史事件 → 当前事件 |
|---|---|---:|---|
| B1 / 9 | Media start failure | 19 | 27 → 16 |
| B1 / 10 | Start rollback failure | 17 | 27 → 31 |
| B1 / 28 | Real task wait and stack cadence | 34 | 11 → 1 |
| B2 / 27 | Wait and stack cadence | 30 | 12 → 1 |
| B2 / 59 | Wait and stack cadence | 26 | 12 → 1 |
| B2 / 91 | Wait and stack cadence | 26 | 12 → 1 |
| B3 / 45 | BOOT wait and stack cadence | 1 | 13 → 1 |
| B3 / 46 | Prepare failure gate | 1 | 13 → 1 |

B1 9/10 的历史事件 27 对应 CameraCapture_Stop，当前首次差异分别出现 Bridge Idle 检查（16）和 GetStats（31），与早期 START 回退停止时序差异一致。B1 28、B2 27/59/91、B3 45/46 当前轨迹的首个差异均为事件 1，即 Platform_TimeGetMs。该取时是 P8.2 轮次钩子的实参，进入钩子前发生，Camera/USB 先于状态上报，Storage 先于下一次等待或未准备分支。相关 Camera/Usb/Storage Task 源码在本轮两个提交之间完全一致。历史 B1 37/39 证据仍保留。

## T2 V2 各阶段

Phase 2/5/6/7/8.1/8.2 原入口在两侧通过。Phase 1/3/4 原全树入口在两侧均失败，未解析符号集完全一致。原执行器递归收集应用全树，也编译后续阶段的文件，导致该阶段未提供的运行端口进入链接。限定为原阶段发布时的源文件清单后，仍使用两侧当前源码和原断言，分别通过 250/250、158/158、154/154。原始失败单独标记 BASELINE_BLOCKED。Resource、Lifecycle、USB/Storage Endpoint、Control Service、Execution Manager、RTOS Transport、九条静态队列、Generation 与 ACK 消费对应矩阵和专项日志。

## T3 / T4 工程和资源

清理前后实际工程隔离副本均通过双核 ARMCC5 Rebuild，CM4 HEX 逐字节相等。旧资格符号不在最终 AXF，CM7 代次符号保留，CM4 未登记。52B/56B O0/O2 ABI 与 Replay 通过。资源数据见独立报告。

## 门禁结论

Cleanup 1.4 的前后对照证据收集完成，基线失败均已复现并定位。原失败仍按 FAIL 或 BASELINE_BLOCKED 判定，完整原入口从未被标记为全绿。实板 NOT_RUN，两项资源闭环 NOT_PROVEN。生产代码、测试断言、Keil 用户配置、历史证据和 main 未修改，等待人工审核。
