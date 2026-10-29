# USB Mode Manager 当前状态机测试

日期：2026-10-29。仅更新隔离测试和结果，不修改正式状态机。被测正式文件的SHA256保存在results.json的source_sha256中。

## 运行方式

```powershell
python Tests/UsbModeManager/run_tests.py
```

ARMCC 5.06 update7 build960编译当前正式USB BSP、Platform、Mode Manager、Framework Initcall与ST Core，Unicorn执行Cortex-M7机器码。实际命令和编译器输出保存在commands.json。

使用可控制的Platform_TimeGetMs，覆盖499/500ms、4999/5000ms和32位Tick回绕。HAL时钟、PCD、GPIO、NVIC继续使用既有隔离桩；不接真实USB主机。每项输入、预期、实际状态、调用计数和Pass/Fail见results.md及results.json。

保留52项既有USB场景，另有32项Mode Manager测试。旧第5项适配当前BSP：不再模拟HAL计算出的名义频率错误，而是清除HSI48RDY，验证正式BSP实际执行的就绪检查。没有修改正式BSP或旧测试源文件。

## 本次实际结果

**84项：80 PASS，4 FAIL，共849次断言。测试运行过程中正式源码没有变化。**

- 52项既有USB场景全部通过。
- 32项Mode Manager场景中28项通过、4项失败，涉及两个根因。
- 测试程序已成功编译链接，ARMCC/ARMLINK无Warning。脚本因存在失败用例返回非零，这是正确行为。
- 本次未重新执行正式CM7/CM4 Target构建；不将测试程序编译等同于正式工程或实板验收。

通过内容：初始化顺序、初次启动、重复启动保护、等待配置成功、首次枚举5秒超时、底层错误、请求只记录、Stop成功及失败、断开499/500ms边界、两个时间边界跨Tick回绕、完整TEST重连及产品名称。

## 失败用例与复现

### 问题1：重新启动后未重置枚举计时

位置：CM7/Platform/usb_mode.c，USB_Mode_Process的STARTING成功分支，约第209行。

| 用例 | 输入及执行 | 预期 | 实际 |
|---|---|---|---|
| 120 | 初次配置成功；tick=1000停止；tick=1500重启 | WAIT_CONFIGURED，state_enter_ms=1500 | WAIT_CONFIGURED，但时间戳仍为1000 |
| 121 | 同上；重启后到tick=6499仍未配置 | 自重启只经过4999ms，应继续等待 | 按旧时间戳算成5499ms，进入ERROR，last_result=-8 |

原因：STOPPING写入断开起始时间，STARTING成功后只改状态，没有更新用于新一轮枚举等待的时间戳。

最小建议：STARTING启动成功后，记录当前平台时间，再进入WAIT_CONFIGURED。测试不直接修改正式源码。

### 问题2：重新启动失败后仍处于STARTING

位置：CM7/Platform/usb_mode.c，USB_Mode_Process的STARTING失败分支，约第203-206行。

| 用例 | 输入及执行 | 预期 | 实际 |
|---|---|---|---|
| 122 | TEST停止并等待500ms；HAL Start返回HAL_BUSY | ERROR，保存SYS_ERR_BUSY | 保存-5，但状态仍为STARTING |
| 123 | 同上，下一轮再次调用Process | 保持错误，HAL Start累计两次（含初次启动） | 又调用一次HAL Start，总计三次 |

原因：失败分支仅保存错误并return，没有提交错误状态；下一轮再次执行同一启动分支。当前实现尚无受控重试策略。

最小建议：失败时进入ERROR，或后续明确设计有限重试；不要在未定义策略时逐主循环重试。测试不直接修复。

## 当前设计限制

- ERROR和WAIT_CONFIGURED状态的Reconnect请求目前返回BUSY。用例124、125按当前代码行为通过；但源码注释仍声称ERROR是恢复入口，注释与实际限制不一致，不能据此宣称故障恢复已完成。
- UVC/MSC尚未接入，目标请求返回NOT_SUPPORTED；本轮只验收TEST重连。
- REGISTERING仍为占位，无Class切换功能。
- 所有模拟通过项都不能证明Windows枚举、物理断开时序、500ms对所有主机的适用性或真实时钟精度。

## 测试文件

- mode_test.c：直接包含正式源码，使用既有HAL桩，新增可控时间与状态观测。
- run_tests.py：ARMCC5编译、Unicorn执行、实际状态/错误/调用次数输出，失败返回非零。
- commands.json：实际命令与工具输出。
- results.json / results.md：当前完整结果，保留4项失败，不伪装通过。
- cm7_keil.log：原框架阶段的历史构建日志，本轮未更新，不作为当前正式Target构建证据。
