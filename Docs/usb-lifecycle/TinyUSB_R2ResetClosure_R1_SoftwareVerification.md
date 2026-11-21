# TinyUSB R2-ResetClosure-R1 — 软件验证

**VERIFIED（软件模型）**：完整最终Keil AXF的新增专项 **29/29、261断言**；含真实非阻塞入队成功、重试成功和预期有界故障。不是29种硬件场景都能正常恢复；EP0 IN抢占等受控门禁的负向测试也计入其中。**NOT_TESTED（实板）**：未烧录、未USBPcap、未验证物理时基或实际调度。

## 最终固件

- VERIFIED：AXF SHA256 `9e751116e2d85bb5159b4d4ddc99fb950c8ff1977682ecf97f1625f49bd53e09`。
- VERIFIED：AXF/MAP/HEX/build.log在 `D:\32\STM32H7\h7_mac_lifecycle\output\TinyUSBReplacement\R2ResetClosureR1-review-9e751116e2d8`；各文件哈希见`Tests/TinyUSBReplacement/reports/R2ResetClosureR1_receipt.json`。
- VERIFIED：完整ARM Compiler5重编0 Error/0 Warning；测试先验证receipt的源码SHA，实际加载Flash LMA并执行ARMCC scatter初始化，不制造RW初值。
- VERIFIED：向量117只指向TinyUSB OTG_FS_IRQHandler；未链接HAL_PCD_IRQHandler、旧USB_Mode/ST Core、StorageTask或动态分配。未新增Task或控制器拥有者。

## 模型/观测契约

VERIFIED：使用修改后的实际DWC2、USBD、MSC及FreeRTOS port/queue/mutex。MMIO只对明确的软件请求响应NAK/EPDISD/Flush；USBRST不自动清EPENA、RXFLVL、BUSY或source。队列填充由最终AXF内`xQueueGenericSend(...,0)`完成，不直接写队列计数。

VERIFIED：队列计数/容量/消息大小字段从该AXF的DWARF `QueueDefinition`取得，测试验证容量32、item20。原生Task send监视记录timeout和BASEPRI，START/END顺序带实际generation；Class入口记录回调次数/上下文。没有Fake DCD替代成功路径，也没有写SUCCESS或清BUSY制造退休。

VERIFIED：抢占测试暂停实际ARM Task指令，保存上下文/栈，执行实际OTG IRQ，再恢复Task；在短锁内的硬件Reset只置raw事件，不违反BASEPRI强行执行ISR。模型只覆盖选定交错，不是完整状态空间证明或真实RTOS调度验收。

VERIFIED：新SETUP成功测试只发一次请求，验证18字节描述符Data IN、SET_ADDRESS或SET_CONFIGURATION/CBW及后续INQUIRY。包括ENUM+SETUP同一IRQ、旧EP0 IN的Reset证明尚未完成、满队列延后发布、最新请求优先、256次tag回绕和32bit序号边界。

## 新专项逐项结果

| # | 场景 | 断言 | 结果 |
| -- | -- | -- | -- |
| 1 | START queue31 accepted | 13 | VERIFIED：PASS |
| 2 | START queue32 retries without fault | 13 | VERIFIED：PASS |
| 3 | END queue31 accepted once | 8 | VERIFIED：PASS |
| 4 | END queue32 retains then retries | 13 | VERIFIED：PASS |
| 5 | END nonretirable full queue bounded fault | 14 | VERIFIED：PASS |
| 6 | START nonretirable full queue retains | 13 | VERIFIED：PASS |
| 7 | duplicate START END exactly one Class Reset | 9 | VERIFIED：PASS |
| 8 | post ENUM descriptor without host retry | 10 | VERIFIED：PASS |
| 9 | post ENUM SET_CONFIGURATION without host retry | 9 | VERIFIED：PASS |
| 10 | post ENUM latest SETUP supersedes previous | 8 | VERIFIED：PASS |
| 11 | post ENUM SETUP under END queue pressure | 12 | VERIFIED：PASS |
| 12 | SETUP retry after queue32 | 7 | VERIFIED：PASS |
| 13 | post ENUM setup with missing EPDISD retains | 9 | VERIFIED：PASS |
| 14 | Reset before closure entry | 6 | VERIFIED：PASS |
| 15 | Reset after group proof before Class callback | 8 | VERIFIED：PASS |
| 16 | Reset preempts native Reset Class callback | 8 | VERIFIED：PASS |
| 17 | Reset after END accepted before DCD commit | 8 | VERIFIED：PASS |
| 18 | Reset hardware arrives inside queue short lock | 8 | VERIFIED：PASS |
| 19 | full queue backend completion fails closed | 6 | VERIFIED：PASS |
| 20 | Reset pending preserves queued backend function | 13 | VERIFIED：PASS |
| 21 | SETUP tag wrap keeps native sequence | 8 | VERIFIED：PASS |
| 22 | SETUP sequence exhaustion retains fault | 6 | VERIFIED：PASS |
| 23 | last native SETUP sequence remains valid | 6 | VERIFIED：PASS |
| 24 | malformed post ENUM SETUP fails closed | 6 | VERIFIED：PASS |
| 25 | post ENUM SETUP waits for old EP0 IN Reset proof | 10 | VERIFIED：PASS |
| 26 | new SETUP preemption of restored active IN remains gated | 8 | VERIFIED：PASS |
| 27 | ENUM and SETUP share IRQ snapshot | 8 | VERIFIED：PASS |
| 28 | Reset OUT IRQ does not steal SETUP W1C | 7 | VERIFIED：PASS |
| 29 | unarmed EP0 synthetic completion retires flags | 7 | VERIFIED：PASS |

VERIFIED：`reports/r2-reset-r1.json`保留各断言、原生queue send参数、发布顺序、Class回调、真实调用、寄存器写、最终阶段和首次错误。29项中队列满且可退休事件可由同Task恢复；不可退休FUNC_CALL占满则保留并超时。fault后原队列内容冻结。

## 全部回归

| 入口 | 本轮结果 | 分类 |
| -- | -- | -- |
| 新R1专项 | 29/29、261断言 | VERIFIED；实际完整AXF |
| 原Reset专项31 | 30/31、574断言 | 1项新SETUP契约变化，见下文；原断言不改 |
| R1B | 33/33、313断言 | VERIFIED；无用例回退 |
| MSC | 12/12、1679断言 | VERIFIED；保留原协议夹具Fake DCD，不用于本轮成功停止证明 |
| 等待边界 | 9/9 | VERIFIED；原断言和C夹具不改，仅补链接依赖 |
| R2-Closure | 24/25、185断言 | 与基线相同，旧活动Reset立即fault断言继续FAIL |
| 原R2 | 17/19、114断言 | 与基线相同，旧Reset/Disconnect立即fault断言继续FAIL |
| 原R1 | 0/2、3断言 | 与基线相同：IN未映射读取、OUT旧断言FAIL保留 |

VERIFIED：逐用例比较见`reports/R2ResetClosureR1_caseComparison.json`。不能把组合PowerShell最后一条命令的退出码当作全部入口PASS；以上结论读取每个case和断言。

## 原Reset31唯一变化的拆解

`new SETUP during pending discarded`：

1. **CONTRACT_CHANGED_REQUESTED**：合法SET_CONFIGURATION不再被丢弃；因此END后已产生新CBW owner，source非0不能被解读为旧资源未退休。新专项验证发布失败前旧owner保持、发布成功后恰好一次Class reset及新CBW正常。
2. **UNRESOLVED/BLOCKED（旧门禁暴露）**：旧脚本仍按“第一次被丢弃”重发SET_CONFIGURATION，未先完成现在已经正常提交的EP0 IN Status。该抢占进入R1B保留的ISR停止失败门禁，后续INQUIRY失败。未修改旧脚本掩盖；新专项分别验证一次请求正常成功、活动EP0 IN抢占受控失败并保留源。

因此不是“所有历史入口全绿”，也不宣称解决所有合法EP0控制抢占。

## 原等待9项的链接适配

VERIFIED：原独立DCD等待夹具没有链接USBD，新增3个发布接口造成第一次链接失败；错误日志已保留。只给`run_r1b_wait_tests.py`的编译文件列表增加`r2_reset_r1_wait_adapter.c`，原9项场景/断言逐字不变。适配器只返回false，**在最终等待AXF中连同其计数变量被链接器剔除**。补充wrapper独立验证它们未参与执行；不是用adapter制造取消成功。

VERIFIED：原入口直接运行9/9通过；`run_r2_reset_r1_wait_tests.py`补充不参与证明。原C测试、Reset31、Closure25、R1B33、MSC12、原R2和R1源码均保留。旧等待runner只有这一行构建依赖变更，由收集器精确比对。

## 复现顺序

在 `D:/32/STM32H7/h7_mac_lifecycle` 分别执行：

```text
python Tests/TinyUSBReplacement/build_r2.py
python Tests/TinyUSBReplacement/run_r2_reset_r1_tests.py
python Tests/TinyUSBReplacement/run_r2_reset_r1_baseline_probe.py
python Tests/TinyUSBReplacement/run_r2_reset_tests.py
python Tests/TinyUSBReplacement/run_r1b_tests.py
python Tests/TinyUSBReplacement/run_stack_tests.py
python Tests/TinyUSBReplacement/run_r1b_wait_tests.py
python Tests/TinyUSBReplacement/run_r2_reset_r1_wait_tests.py
python Tests/TinyUSBReplacement/run_r2_closure_tests.py
python Tests/TinyUSBReplacement/run_r2_firmware_tests.py
python Tests/TinyUSBReplacement/run_r1_tests.py
python Tests/TinyUSBReplacement/check_r2_default.py
python Tests/TinyUSBReplacement/audit_upstream.py
python Tests/TinyUSBReplacement/collect_r2_reset_r1_evidence.py
```

VERIFIED：上述入口均实际执行；预期历史FAIL仍返回非零。原始reports、history快照和output未清理，包括本轮最初1 Warning、缺失符号链接失败、新夹具观测/轮次错误等开发证据。最终验收基于本报告AXF哈希，不能用早期产物代替。

NOT_TESTED：实际芯片GSNPSID、真实IRQ延迟、主机时序、DWT精度、持续负载、物理Reset/拔插和Windows枚举。v3.10a合成状态测试是条件性DWC2模型，不证明板上IP版本一定为v3.10a。
