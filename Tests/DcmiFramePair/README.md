# DCMI TC / FRAME配对验收

## 实现
- Dcmi_StreamTryPair由M0 TC、M1 TC和FRAME三个入口共同调用。
- 两种pending齐全才检查错误，确认completed_target与CT指示的活动目标不同。
- 成功时paired_frames加一并清除两个pending，重复调用不重复计数。
- 错误时不计数、不消费pending，锁存错误并要求Stop；不交付Media。
- 修正M1完成检查：M1完成后CT应为0；M0完成后CT应为1。
- 每轮Continuous启动清除旧配对状态和计数。

## 测试
```powershell
python Tests/DcmiFramePair/run_tests.py
```
- 真实BSP与HAL经ARMCC5编译、Unicorn执行：{'total': 82, 'passed': 82, 'failed': 0, 'assertions': 7104}。
- 隔离测试编译链接0 Error、0 Warning。
- CM7、CM4正式Target增量编译均0 Error、0 Warning；日志见本目录。
- 前62项保留Snapshot及Stop状态提交回归，新增20项针对配对。

## 限制
- 三个入口依赖DCMI与DMA IRQ同抢占优先级，不允许在其他未保护上下文调用。
- CT仅反映读取时的活动目标，不能单独发现DMA已经完整往返两次；尚不构成任意IRQ延迟下的数据有效性证明。
- 本轮未实现地址替换、Media通知和完整连续采集验收，未进行实板操作。

## 结果
|编号|场景|结果|
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
|63|No events|PASS|
|64|TC only|PASS|
|65|FRAME only|PASS|
|66|M0 TC then FRAME|PASS|
|67|FRAME then M0 TC|PASS|
|68|M1 TC then FRAME|PASS|
|69|FRAME then M1 TC|PASS|
|70|M0 invalid CT|PASS|
|71|M1 invalid CT|PASS|
|72|Invalid target|PASS|
|73|DCMI error blocks pairing|PASS|
|74|DMA error blocks pairing|PASS|
|75|Duplicate TC|PASS|
|76|Duplicate FRAME|PASS|
|77|CT switched before pair|PASS|
|78|20 alternating pairs|PASS|
|79|Fault gate|PASS|
|80|Snapshot gate|PASS|
|81|Start resets pairing|PASS|
|82|Error does not rearm FRAME|PASS|
