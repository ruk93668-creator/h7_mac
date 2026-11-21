# TinyUSB R2-Closure 软件验证

**总体BLOCKED：活动Bus Reset没有成功证明；原R2一项旧断言保留FAIL。** 通过的正向关闭/重连模型检查和这些未达成项分别报告。

| 入口 | 结果 | 证明边界 |
| --- | --- | --- |
| build_r2.py | VERIFIED：完整Keil AXF/MAP/HEX，0 Error、0 Warning | 包含真实Startup/FreeRTOS/HAL/MSC/DCD，不是组件替代 |
| run_r2_closure_tests.py | VERIFIED：25/25，185断言，exit0 | 实际完整AXF，包含成功和受控失败；Reset项只检查拒绝 |
| run_r1b_tests.py | VERIFIED：33/33，313断言，exit0 | 原断言保留；真实DCD/Core ARM对象 |
| run_stack_tests.py | VERIFIED：12/12，1679断言，exit0 | 原MSC协议回归，Fake DCD，非硬件停止证明 |
| run_r1b_wait_tests.py | VERIFIED：9/9观察通过，exit0 | 全部函数实际返回；模拟器指令预算只作watchdog |
| run_r2_firmware_tests.py | VERIFIED：18/19，114断言中1项FAIL，exit1 | 旧19项测试正文未改；见下表契约冲突 |
| run_r1_tests.py | VERIFIED：0/2，原FAIL保留 | 旧fixture未映射DWT/旧立即FAILED假设，未修改断言 |
| compile_probe.py | VERIFIED：18对象0错误0警告，exit0 | 补充组件回归，不代表SD/UVC完整接入 |
| audit_upstream.py | VERIFIED：52原始Git blob核验 | 上游版本未升级，本地累计补丁单独保存 |

## 新的成功路径

VERIFIED（完整ARM AXF模型）：

- 活动Data IN和Data OUT各自取消配置：Pending期间旧source保留、不提前Status；完成后SET_CONFIGURATION(0)，再次配置、CBW、INQUIRY/CSW正常。
- 新GET_DESCRIPTOR抢占旧取消配置：不再补发旧Status，新EP0的18字节传输、完成身份和OUT状态阶段保持；新配置也能替换旧待配置请求，只有最新请求提交一次Status。
- 活动物理SEDET和软Disconnect各自退休后Connect/空闲Reset/配置，CBW及INQUIRY恢复；未强制重启Controller。
- 活动IN/OUT直接DeInit经Task停止后再次成功DeInit/Init；十轮重复，generation单调、新CBW正常。
- 旧Backend IO跨取消配置/新配置继续持有Buffer；只有实际RamPoll和MSC异步完成后才收新CBW。
- EP1/EP2 OUT同时退出，唯一NAK owner逐个归还；Fence之后的旧RX被消费但不写旧buffer。

VERIFIED：CSRST不完成时保留Class字节、NVIC未被关闭、GINT恢复、pullup未被提前关闭；缺NAK、缺Disable、外来NAK owner、停止时钟均有界失败，旧source/busy和故障保留，Status未提交。EP0活动时DeInit拒绝并保留IRQ/source；正常控制完成之后才能退出。Reset与组停止交错仍按未证明条件拒绝。

## 旧19项的单个契约冲突

| 项 | 原断言 | 新实际结果 | 分类 |
| --- | --- | --- | --- |
| active Disconnect boundary | SEDET IRQ一结束就要求usbd_device_failed()==1 | 返回未故障、Pending；旧BUSY仍保留，后续Task可正确退休 | CONTRACT_CHANGED：用户明确要求正常Disconnect闭环 |

VERIFIED：没有把该断言改成0，也没有删除旧用例；原19项正文与基线逐字节一致（仅统一CRLF比较）。新增正向用例验证新的Pending→退休→重连，而旧FAIL原样保留。因而不能宣称所有旧测试全绿；旧断言迁移留待人工审核。其他18项原断言继续通过，R1B/MSC/等待检查无新失败。

## 加载器修复及失败历史

VERIFIED：新的RW布局触发ARMCC压缩scatter数据。旧加载器只装SHT_PROGBITS到VMA，造成_usbd_rhport错误为0、Init错误地被认为已完成、队列/Mutex为空。现在装入真实PT_LOAD的LMA字节，并执行AXF本身__scatterload/decompress直到__rt_entry；随后才提供显式的模型时钟和scheduler-ready环境。没有手写USB已初始化值，没有直接设置SUCCESS、清busy或修改生产引用。

VERIFIED：此加载器修复仅改旧测试的初始化夹具；19项断言正文不变。初次RW加载失败、IN NAK通知导致的readiness拒绝、旧Status上下文问题、首次soft Disconnect符号被链接裁剪等失败均留在*-history-*.json和原output。软Connect/Disconnect现经原实验Task的可选命令3/4真实引用，进入同一最终固件，未用额外测试DCD对象冒充。

模型仍不是板级证明：真实HAL启动/PLL、完整FreeRTOS调度抢占、实际NAK/EPDISD/FIFO时间均NOT_TESTED。输入只包含MMIO/时钟/中断事实，实际生产DCD、Core、MSC、FreeRTOS队列及Port执行原代码。Status检查表示DCD提交，不能等同Windows实际ACK。

证据：[专项](../../Tests/TinyUSBReplacement/reports/r2-closure.json)、[旧19项](../../Tests/TinyUSBReplacement/reports/r2-firmware-tests.json)、[完整构建](../../Tests/TinyUSBReplacement/reports/r2-build.json)、[R1B](../../Tests/TinyUSBReplacement/reports/r1b.json)、[MSC](../../Tests/TinyUSBReplacement/reports/stack.json)、[等待](../../Tests/TinyUSBReplacement/reports/r1b-waits.json)、[收据](../../Tests/TinyUSBReplacement/reports/R2Closure_receipt.json)。复验入口均位于Tests/TinyUSBReplacement，collect_r2_closure_evidence.py检查最终源码/AXF哈希、原断言及禁止模块边界，并保留全部产物。

VERIFIED：新增Connect-after-DeInit检查，未重新Init时返回false且不触及invalid rhport。最终专项25项/185断言；新增测试和所有历史失败一起提交。
