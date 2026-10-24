# DCMI Stop状态提交验收

## 修改
- 保留Dcmi_WaitStopped和Dcmi_RecoverHal顺序。
- 最终状态提交前合并HAL_DCMI_Stop结果，保留先发生的停止/恢复错误。
- 全部成功才提交NONE/STOPPED，清理上下文、恢复IRQ。
- 失败保留原模式和上下文；Continuous进入ERROR并要求Stop；全局错误锁存，IRQ保持关闭。

## 执行
```powershell
python Tests/DcmiStopCommit/run_tests.py
```
- ARMCC 5.06 update 7 (build 960)，编译实际BSP和HAL，Unicorn执行ARM代码。
- 62项全部通过，6942次断言。隔离测试编译链接0 Error、0 Warning。
- CM7、CM4正式Target增量构建均0 Error、0 Warning，日志保存在本目录。

## 范围
- 46项Snapshot回归，其中HAL Stop失败的3项旧预期按本次新约定调整。
- 16项状态提交测试：Snapshot/Continuous各覆盖成功、DMA停不下、4种HAL恢复失败、HAL Stop超时和IO错误。
- 验证失败后门控、上下文保留、IRQ关闭、再次Stop成功及Snapshot重新启动。
- Continuous场景直接建立运行上下文和DMA活动条件；连续采集启动仍未完成，未声称真实Continuous图像采集通过。
- 无实板操作；模拟不验证DVP时序和物理DMA行为。

## 逐项结果
|编号|用例|结果|
|---|---|---|
|1|未初始化拒绝调用|PASS|
|2|初始化不自动采集|PASS|
|3|参数边界和对齐|PASS|
|4|ISR拒绝启动/停止|PASS|
|5|关中断拒绝启动/停止|PASS|
|6|空闲Stop|PASS|
|7|清理旧事件和硬件挂起|PASS|
|8|Active重复Start拒绝|PASS|
|9|DMA完成尚无Frame|PASS|
|10|正常Snapshot完成|PASS|
|11|连续两帧和ERR/OVR重启|PASS|
|12|同步错先通知再Stop|PASS|
|13|溢出先通知再Stop|PASS|
|14|错误和Frame同时到达|PASS|
|15|真实DMA TE错误|PASS|
|16|纯FIFO错误回调|PASS|
|17|过滤其他句柄回调|PASS|
|18|早到Frame拒绝|PASS|
|19|Start HAL_ERROR|PASS|
|20|Start HAL_BUSY|PASS|
|21|Start HAL_TIMEOUT|PASS|
|22|Abort无回调仍能Stop恢复|PASS|
|23|HAL Stop虚假成功仍核对DMA|PASS|
|24|HAL Stop超时但恢复硬件|PASS|
|25|HAL Stop错误但恢复硬件|PASS|
|26|DMA无法停下时超时并阻止Start|PASS|
|27|恢复DMA DeInit失败|PASS|
|28|恢复DMA Init失败|PASS|
|29|恢复DCMI DeInit失败|PASS|
|30|恢复DCMI Init失败|PASS|
|31|ABORT+Lock遗留恢复|PASS|
|32|Stop后迟到回调丢弃|PASS|
|33|NULL Take不消费|PASS|
|34|读清期间完成IRQ延后交付|PASS|
|35|读清期间新错误不丢失|PASS|
|36|PRIMASK=1保持及中断延后|PASS|
|37|未处理硬件错误覆盖FRAME|PASS|
|38|Stop保留已完成事件|PASS|
|39|控制接口重入BUSY|PASS|
|40|真实异步Abort完成IRQ|PASS|
|41|遗留DCMI Lock停止恢复|PASS|
|42|Stop等待Tick回绕|PASS|
|43|新Start清除旧完成|PASS|
|44|读出错误后Frame仍不变成功|PASS|
|45|非法Start不清事件|PASS|
|46|重复Stop不产生完成事件|PASS|
|47|Snapshot: Normal Stop|PASS|
|48|Snapshot: DMA stuck|PASS|
|49|Snapshot: DMA DeInit error|PASS|
|50|Snapshot: DMA Init error|PASS|
|51|Snapshot: DCMI DeInit error|PASS|
|52|Snapshot: DCMI Init error|PASS|
|53|Snapshot: HAL Stop timeout|PASS|
|54|Snapshot: HAL Stop IO error|PASS|
|55|Continuous: Normal Stop|PASS|
|56|Continuous: DMA stuck|PASS|
|57|Continuous: DMA DeInit error|PASS|
|58|Continuous: DMA Init error|PASS|
|59|Continuous: DCMI DeInit error|PASS|
|60|Continuous: DCMI Init error|PASS|
|61|Continuous: HAL Stop timeout|PASS|
|62|Continuous: HAL Stop IO error|PASS|
