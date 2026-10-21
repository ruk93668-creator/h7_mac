# CM4 I2C2 BSP 通信隔离验收

本目录为新增隔离测试，不加入 Keil 正式 Target。正式 BSP、头文件、Framework、Device、OLED 和 Scatter 均不修改。

从工程根目录运行：

```powershell
python Tests/BspIicCommunication/run_tests.py
python Tests/BspIicCommunication/audit_project.py
```

`run_tests.py` 自动从既有 `$env:TEMP/h7_timer_latest_audit.txt` 查找测试专用 Unicorn 安装目录，也可以明确指定：

```powershell
python Tests/BspIicCommunication/run_tests.py --deps 'C:/Users/29062/AppData/Local/Temp/h7_timer_acceptance_82dd1ace6fe14c8d8206dca43fb4da64/python_deps'
```

依赖：Python 3、pyelftools、Unicorn、`D:/32/ARM/ARMCC/Bin/armcc.exe` 和 `armlink.exe`。依赖路径失效时需提供现有可用的 Python 包目录；测试脚本不自动修改系统环境。

正式 CM4 构建实际使用的命令（PowerShell）：

```powershell
$p = Start-Process -FilePath 'D:\32\UV4\UV4.exe' -ArgumentList @('-r','D:\32\STM32H7\h7_mac\MDK-ARM\h7_mac.uvprojx','-t','h7_mac_CM4','-o','D:\32\STM32H7\h7_mac\Tests\BspIicCommunication\cm4_keil.log') -WindowStyle Hidden -PassThru
$p.WaitForExit()
```

## 实现与证据

- `communication_test.c` 直接 `#include` 当前真实 `CM4/BSP/bsp_iic.c`，不复制或改写其状态机；真实头文件及 Initcall 宏参与编译。
- `stubs/stm32h7xx_hal.h` 仅替代硬件依赖和 CMSIS PRIMASK 接口；HAL 状态/错误值对应当前本地 HAL 定义。
- 通过真实 BUS 注册项调用初始化。测试使用静态符号检查内部状态、事务地址、复制缓冲区及 NVIC；不改 BSP 的函数实现。
- ARMCC5 生成 ARM 可执行 ELF，Unicorn Cortex-M 模式执行。每例独立内存；栈、初始化数据与零初始化数据分别建立。没有声称执行完整正式固件。
- 事件先进入普通 IRQ 待投递槽，只有模拟 PRIMASK=0 且相应 NVIC 使能时才投递，经真实 `I2C2_EV/ER_IRQHandler()`、Mock HAL IRQ、真实 BSP 回调完成处理。当前测试每次最多一个待投递事件，不模拟多 IRQ 排队优先级。
- T07/T18/E05 用例断言屏蔽期间不执行回调，恢复后再执行；T17/E08/E09 在不同合法指令边界注入完成中断。
- 没有增加或修改超时、自动恢复、队列或正式状态机。测试编译所需的独立 C runtime 内存桩不属于业务实现。

输出：

| 文件 | 内容 |
|---|---|
| `results.md` | 每例输入、预期、实际、PASS/FAIL |
| `results.json` | 全部断言、对应 C 源码行/表达式、IRQ/PRIMASK 时间线 |
| `commands.json` | 实际编译命令、编译器版本、退出码及输出 |
| `execution_output.txt` | 实际运行摘要 |
| `communication_test.axf` | 实际执行的独立 ARM 测试程序 |
| `cm4_keil.log` | 正式 CM4 全量构建日志 |
| `project_audit.json` | 正式文件哈希、Initcall、向量和符号检查 |
| `source_before.json` | 测试前正式源文件和配置文件哈希基线 |
| `验收报告.md` | 最终结论、限制及硬件测试建议 |

`results.json` 的 `callback_cached_result` 是 BSP 内部 `s_tx_result`，不等于每次 `TakeResult()` 的输出参数。Abort 启动失败直接写调用者输出参数，T08/T09 的对应断言单独验证返回 true、输出 -9。

## 测试工具调试说明

最初测试 runtime 包装函数调用标准 `memset/memcpy`，被 ARMCC 内建替换为同一 AEABI 包装，造成递归和仿真栈溢出。已仅在测试 runtime 内改为直接 volatile 字节循环，消除工具桩问题；未改 BSP 或 Mock 业务返回行为。最终测试重新编译、执行，29/29 PASS；工具调试异常不计为 BSP 失败。
