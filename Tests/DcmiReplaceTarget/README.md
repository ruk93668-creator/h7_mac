# BSP_DCMI_ReplaceTarget验收

最新补充：旧目标缓冲区禁止同址或部分重叠，当前133/133通过，两核增量构建均0 Error、0 Warning。详见[补充验收](old_overlap_report.md)。以下保留初次实现验收记录。

## 实现
- 保留当前用户新增的完整帧回调及replace_allowed授权机制。
- 检查目标编号、NULL、4字节对齐、整帧地址溢出及与另一DMA目标重叠。
- 仅接受Continuous RUNNING、采集有效、本帧双pending齐全且授权目标匹配的调用。
- 临界区内检查DMA EN、DBM、CT、NDTR、TC挂起和错误。
- 调用真实HAL_DMAEx_ChangeMemory；DSB后读回地址并复查CT、EN/DBM、NDTR、TC与错误。
- 成功消费本帧授权；硬件异常锁存错误，保留错误状态要求Stop，不尝试回滚不确定的地址。
- 无效参数或非授权调用在写入前返回INVALID_ARG/INVALID_STATE，不主动改变硬件。

## 门限与边界
- 切换前后NDTR都必须大于32 Word，且写后NDTR不大于写前值。
- 32 Word为保守的初始门限，不是经过当前PCLK/DMA仲裁最坏时序证明的时间保证，实板测试必须确认。
- 关中断不能暂停DMA。前后检查只能发现可观察的窗口错过，不能证明任意延迟或多次DMA周转下绝对安全。
- 不验证DMA可访问地址区、真实缓冲区容量和MPU/Cache属性；由调用方保证。
- 授权由同步回调作用域控制，不要求调用方改变公开接口。

## 验证
```powershell
python Tests/DcmiReplaceTarget/run_tests.py
```
- 总计119项，PASS 119，FAIL 0，断言7392。
- 前82项为Snapshot、Stop和配对回归；为适配现有帧回调，旧配对测试补充了不操作地址的通知桩。
- 新增37项覆盖参数、状态、地址、授权、硬件切换、HAL失败及回调内替换集成。
- 真实BSP和HAL通过ARMCC5编译，使用Unicorn执行ARM代码；ChangeMemory故障通过测试包装桩注入。
- 测试程序编译链接0 Error、0 Warning；CM7正式Target增量构建0 Error、4 Warning；CM4为0 Error、0 Warning。详见构建日志。
- CM7的4项Warning为未引用的CameraStream_Init、CameraStream_OnFrameComplete、CameraStream_GetFrame、CameraStream_ReleaseFrame；bsp_dcmi.c无警告。
- 无实板操作，未将Media接入连续采集。

## 每项结果
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
|83|M0 success|PASS|
|84|M1 success|PASS|
|85|NULL buffer|PASS|
|86|Unaligned buffer|PASS|
|87|Invalid target|PASS|
|88|No callback grant|PASS|
|89|Wrong mode|PASS|
|90|Not RUNNING|PASS|
|91|Wrong completed target|PASS|
|92|DMA disabled|PASS|
|93|Not DBM|PASS|
|94|Target active|PASS|
|95|Insufficient pre margin|PASS|
|96|NDTR exceeds frame|PASS|
|97|Address overflow|PASS|
|98|Partial overlap|PASS|
|99|Same active buffer|PASS|
|100|HAL ERROR|PASS|
|101|HAL BUSY|PASS|
|102|HAL TIMEOUT|PASS|
|103|Write ignored|PASS|
|104|CT changes during write|PASS|
|105|NDTR reloads|PASS|
|106|Post margin missed|PASS|
|107|DMA stops during write|PASS|
|108|DBM clears during write|PASS|
|109|New DCMI error|PASS|
|110|New DMA error|PASS|
|111|New TC pending|PASS|
|112|Second replacement rejected|PASS|
|113|PRIMASK preserved|PASS|
|114|Normal NDTR decrement|PASS|
|115|Pre DCMI error|PASS|
|116|Pre DMA error|PASS|
|117|Pre TC pending|PASS|
|118|FRAME-M0 callback replacement|PASS|
|119|M1-FRAME callback replacement|PASS|
