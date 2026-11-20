# P1-C — 完整软件验证报告（C0完成，C1受阻）

基线：03a5cb1793e35ec23728f3a6ebbdc132277ed597。VERIFIED：所有生产文件和原测试入口保持基线字节；本轮新增C0证据工具及文档。UNRESOLVED：Bulk OUT安全停止生产实现按C0门禁暂停，没有新Begin/Poll/Commit可以宣称通过。

## 专项与基线

| 组 | 结果 | 证据性质 |
| --- | --- | --- |
| 原P1-B Bulk IN | VERIFIED，38/38通过、796断言 | 原夹具/断言未修改，隔离快照执行 |
| 原P1-B.1 Service/恢复集成 | VERIFIED，38/38通过、362断言 | 原夹具/断言未修改，原76项共1158断言 |
| 新Bulk OUT C0真实HAL/LL探针 | VERIFIED，24/24通过、99断言 | 未修改HAL/LL编译为真实ARM对象，Unicorn MMIO模型；不是C1 Stop验收 |
| C1全部安全停止/恢复 | NOT_TESTED | 门禁未通过，未增加生产接口，不用模型直接清busy伪造成功 |
| 实板/USBPcap/插拔/烧录 | NOT_TESTED | 未执行 |

VERIFIED：C0检查GRXSTSR peek和GRXSTSP pop、RXFLVL在EPENA=0/DAINT masked时仍copy、1字节及31字节尾包精确消费/邻接字节保护、EP0 Setup及多端点共享队列、任意OUT EPDISD解除共享NAK、ISO请求及取消、NAK marker消费条件、正常XFRC回调和旧raw flag、回调重Arm与缓存EPDISD、IN完成、RX先于Reset、旧LL Abort迭代失败。每项输入、状态、读写寄存器、copy次数及预期/实际断言在 `Tests/UsbBulkOut/reports/hal_evidence.json`。

INFERRED模型假设：W1C、队列及NAK marker效果按RM0399建立。310A模型和300A分支分别执行；并未读取实际OTG GSNPSID。注入raw事件组合是软件探针，不等于该组合在硅片上必然出现。C0不调用新的生产Stop函数，因为它尚未实现。

## 三配置完整重编

VERIFIED：ARM Compiler 5.06 update7(build960)，现有MscReconnect/build.py在全新隔离快照完整构建；没有修改原Keil/RTE/MPU/Linker。三配置均0 Error、0 Warning；WRITE最终预处理值均0，SD只读构建不含adapter写调用。AXF/HEX/MAP及完整编译命令/日志保留在output/MSC24/udlm-p1c/build-p1c-c0；原始构建记录在 `Tests/UsbBulkOut/reports/build-p1c-c0.json`。

| 配置 | BOOT/SD/WRITE | 编译结果 | AXF SHA256 |
| --- | --- | --- | --- |
| ram | 1/0/0 | VERIFIED，0 Error / 0 Warning | `d8a8e854a024407b18f682cda5e384dd1834f922e8cfa175cb0c67add8ac0e39` |
| sd_readonly | 1/1/0 | VERIFIED，0 Error / 0 Warning | `45ddea3c36bedc67f2870bd9cb636d41a0b56cfdbde5f82cb99bcd59dc0ca399` |
| uvc | 0/0/0 | VERIFIED，0 Error / 0 Warning | `1711d81fb92409943e5a4a74fab503b294c1b90d77af3067b9f0c53fb4138892` |

VERIFIED：构建快照与本地审计源码哈希一致，MAP保留P1-B.1真实生产停止/恢复入口；本轮没有新OUT停止符号，不能据编译成功宣称C1集成成功。不同快照绝对路径会影响含调试信息AXF哈希，不将它与旧目录产物哈希不同误报为生产改动。

## 原17个历史入口逐用例对照

VERIFIED：17个入口全部执行，6成功、11失败；相比03a5cb1无新增FAIL/丢失用例。原始失败、失败行、actual/expected、检查数量及结果保存在regression-p1c-c0.json和history_comparison.json。4项EP0竞争结果因原字节相同而未被快照驱动重复输出，已从真实运行快照补入逐用例对照，没有只看退出码。

| 入口 | 结果 |
| --- | --- |
| UsbCoreDecoupling | VERIFIED，204项196 PASS/8 FAIL，保留109/110/201/334/335/361/362/364 |
| UvcLifecycle | VERIFIED，40项34 PASS/6 FAIL |
| UvcStreaming | VERIFIED，36项1 PASS/35 FAIL |
| UvcPayload | VERIFIED，124次122 PASS/2 FAIL，原case37 O0/O2保留 |
| UvcEp1Lifecycle | VERIFIED，45项全部FAIL |
| UvcProbeCommit | VERIFIED，89项全部FAIL |
| UvcProbeCommit cancellation | VERIFIED，39项全部FAIL；其导入运行的89项也保留，不重复算新测试 |
| Ep0IrqRace | VERIFIED，4项PASS |
| UsbUvcDescriptors | VERIFIED，入口FAIL，保留缺camera_mjpeg_fifo.h的旧子构建错误；Core字符串8项PASS |
| UvcUsbCleanup | VERIFIED，14项全部FAIL |
| MscDeferredStatus | VERIFIED，17项PASS |
| MscProtocol | VERIFIED，41项PASS |
| MscBlockAdapter | VERIFIED，18项PASS |
| MscSdIntegration | VERIFIED，24项PASS，软件模型 |
| MscEnumeration unit / integration | VERIFIED，分别原11/8项链接错误，未运行行为测试；NOT_TESTED行为验收 |
| MscSenseCompat | VERIFIED，18项PASS |

没有修改旧断言或将FAIL改成预期成功。对照工具读取原用例JSON并保存全部状态，不以“6/11退出码没变”代替逐用例证据。

## 初始夹具失败与保留现场

VERIFIED：最初链接缺__aeabi_memset、HAL_PCDEx_ActivateLPM、HAL_PCDEx_LPM_Callback、HAL_RCC_GetHCLKFreq。新增的只为测试运行库/LPM/RCC边界，不改HAL/LL。该启动失败信息来自实际工具输出，转录于bootstrap_failures.json，非声称有不存在的原编译日志。

VERIFIED：首轮19项因测试运行库helper问题全部UC_ERR_WRITE_UNMAPPED，原始PC、状态及源码哈希保存在hal-evidence-history-*.json；改成直接volatile循环后通过。INFERRED原因是编译器把helper内memset调用转写为ABI helper导致递归；未把推断写成硅片故障。所有后续历史JSON/compile-history及output保留，没有删旧失败或改断言。

## C0门禁与待审核事项

UNRESOLVED：唯一Global NAK owner、真实RX pop/copy引用、W1C前事件归属等见[未解决条件](USB_BulkOUT_UnresolvedConditions.md)。请审核[最小接口扩展申请](USB_BulkOUT_MinimalInterfaceExtension_Approval.md)，未批准前不修改HAL/LL/Core，不复制HAL IRQ绕过。

VERIFIED：本轮Diff限定新增Docs/usb-lifecycle文件与Tests/UsbBulkOut工具/报告；生产文件及原文档原样。RM0399原PDF未跟踪且哈希不变，未操作其他工程目录，未清理output、未stash/reset/clean、未合并main。远端网络核验及提交推送结果以实际Git交付为准；网络失败不得声称已推送或SHA一致。

完成本轮已授权C0/软件证据后停止等待人工审核，不进入MSC BOT迁移。C1整体结论为UNRESOLVED，不是PASS。
