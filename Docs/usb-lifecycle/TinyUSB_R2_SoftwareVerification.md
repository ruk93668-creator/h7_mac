# TinyUSB R2 软件验证

总体R2验收：**BLOCKED**。软件观察通过不等于所有功能要求满足。

| 验证 | 最终结果 | 边界 |
| --- | --- | --- |
| Keil完整RAM MSC重编链接 | VERIFIED：0 Error、0 Warning | Startup/RTOS/HAL/DCD/Core/MSC均进入真实AXF |
| 完整AXF执行 | VERIFIED：19/19，114断言 | 真实FreeRTOS Port/队列/Mutex和DCD，不是Fake DCD；寄存器和调度准备状态为模型 |
| R1B专项 | VERIFIED：33/33，313断言 | 原断言不修改，正常SUCCESS/异常FAILED/Status资格 |
| 原MSC | VERIFIED：12/12，1679断言 | 原Fake DCD协议回归，不作硬件证明 |
| 等待边界 | VERIFIED：9/9全部返回 | 包括原6无返回反例；ZLP项只是原顺序观察 |
| 旧R1两项 | VERIFIED：0/2，原FAIL保留 | 一项未映射DWT的fixture非法读，一项旧立即FAILED断言不适用PENDING |
| 三模式组件复编 | VERIFIED：18对象0错误0警告 | 补充回归，不代替完整RAM固件；未接入SD/UVC |
| 实板、Windows、USBPcap | NOT_TESTED | 无烧录、无物理操作 |

## 完整固件执行方法

VERIFIED：`run_r2_firmware_tests.py`加载完整Keil AXF的可加载节，执行其中实际符号。每个源SHA先与构建receipt核验，防止误执行陈旧AXF。没有替换生产DCD或直接清busy/写proof/设置SUCCESS。模型仅提供MMIO转移、DWT读取递增及正常启动后应有的FreeRTOS critical nesting/NVIC资格，scratch buffer位于DTCM范围。

VERIFIED：实际执行嵌套usbd_spin_lock→vPortEnterCritical/ExitCritical，读取真实BASEPRI0x50/0；检查Pending USB优先级是否被屏蔽，并执行实际ISR分支。真实xQueueGenericCreateStatic和xQueueCreateMutexStatic被使用，句柄非NULL，最终ELF没有pvPortMalloc。NOT_TESTED：真实调度器抢占、完整启动时钟循环、CPU2运行干扰；该实验保持CM4休眠。

VERIFIED：通过唯一RXFLVL路径注入SETUP/CBW和正常完成，检查实际TX FIFO内容里的描述符、SCSI响应、CSW签名/Tag/Residue/Status。RAM WRITE后实际READ回读512字节一致；GET_MAX_LUN、BOT Reset和Clear HALT均经实际Core/MSC/DCD处理。RAM异步完成走原Backend→TinyUSB_RamPoll→tud_msc_async_io_done。

## 功能门禁不是绿色测试

- VERIFIED：测试名 `active Bulk Bus Reset boundary` 的PASS只表示**观察到预期的安全故障与source保留**；正常活动Reset后重新配置仍BLOCKED。
- VERIFIED：`active Disconnect boundary`同样只验证故障保留；重连枚举仍BLOCKED。
- VERIFIED：`close active group preserves fault`验证拒绝活动void关闭；不代表活动组关闭完成。`close already retired group`才验证正常已退休关闭。
- VERIFIED：正常取消后DeInit/Init成功；Pending DeInit拒绝。DeInit硬件失败时IRQ推进能力仍BLOCKED。

明确门禁列表见[R2_link_and_gates.json](../../Tests/TinyUSBReplacement/reports/R2_link_and_gates.json)。不能将19项观察全绿表述为R2功能全部通过。

## 失败证据与可复验入口

所有入口在 `Tests/TinyUSBReplacement/`：`build_r2.py`、`run_r2_firmware_tests.py`、`run_r1b_tests.py`、`run_r1b_wait_tests.py`、`run_stack_tests.py`、`run_r1_tests.py`、`compile_probe.py`、`audit_upstream.py`。使用现有ARMCC5/Keil和output内既有Unicorn依赖，不提交依赖库或构建产物。

VERIFIED：JSON覆盖前按hash保留history。初次完整构建的1条MPU_ConfigSdram未引用警告已通过实验编译门控修复；未屏蔽告警。首次完整AXF模型调用了被编译器inline而不导出的tud_mounted，错误记录保留，后改用真实generation/CBW行为检查。中间build receipt脚本因Path变量遮蔽process变量报错，历史日志/说明保留；修复后重新完整构建、核对源码SHA并重跑，没有沿用旧AXF充当最终证明。

证据：[完整构建](../../Tests/TinyUSBReplacement/reports/r2-build.json)、[AXF执行](../../Tests/TinyUSBReplacement/reports/r2-firmware-tests.json)、[R1B](../../Tests/TinyUSBReplacement/reports/r1b.json)、[MSC](../../Tests/TinyUSBReplacement/reports/stack.json)、[等待](../../Tests/TinyUSBReplacement/reports/r1b-waits.json)、[旧R1 FAIL](../../Tests/TinyUSBReplacement/reports/r1.json)、[组件](../../Tests/TinyUSBReplacement/reports/compile.json)。所有历史output保留，原测试断言未放宽。

VERIFIED：`check_r2_default.py`对main/UsbTask默认分支与R1B作预处理比较，仅排除configASSERT的诊断文件/行号差异，代码token一致。两次探针校准（Tasks include缺失、诊断位置不同）均在[R2_default_behavior.json](../../Tests/TinyUSBReplacement/reports/R2_default_behavior.json)记载；没有改生产逻辑以迁就比较。
