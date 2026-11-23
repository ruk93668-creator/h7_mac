# BLE-B1 CM7 USART2 实现与验收交接

日期：2026-11-22。分支：`codex/ble-rtl8723bs`。生产基线：`935973c`（生产代码仍为 B0 所审计版本）。用户已授权 B1，并接受 CTS 接地丢包风险及异常时整板断电恢复限制。**B1 实板代码验收 NOT_VERIFIED；B2 未启动、未获授权。**

## 实现范围

- 新增 `CM7/BSP/bsp_bt_uart.c/.h`，只操作 CM7 USART2、PD5 TX/PA3 RX AF7；IRQ38，优先级7，无 DMA、无 RTS/CTS、无 WAKE/模块复位操作。
- 115200、8 数据位、偶校验、1 停止位。核对 USART2 时钟源为 PCLK1 后读取实际 PCLK1 频率，16倍采样、PRESC=1，根据实际频率计算 BRR，并检查约2%误差上限；不修改共享时钟 mux 或 PLL。
- 2 KiB RX、1 KiB TX 静态 ring；单次 API 在短临界区最多复制64字节。索引差值和2次幂容量支持循环使用；不阻塞等待队列空间，不创建 Task。
- `Init` 等待 TEACK/REACK，具有10ms Tick上界及100000轮询兜底；等待期间中断打开。初始化/恢复使用 busy 状态，直到发布完成后才开启 USART2 IRQ。
- RX/错误中断，TXE 按需启停；每次 IRQ 最多32次 RX 读取、1字节 TX，不调用 RTOS、分配器或日志输出。
- ORE、FE、PE、NE 统计并锁存；RX ring overflow 单独统计。错误后丢弃已缓存 RX/待发 TX，禁止中断风暴，Read/Write 返回 IO，必须显式 Recover。
- Recover 清缓冲/标志并重新启用 UART，保留累计错误统计；超时释放 UART。它不恢复 H5，也不复位 RTL8723BS。
- DeInit 是有界强制取消；清队列，恢复两个引脚的原始字段、NVIC优先级及原UART时钟门控。不复位整个 GPIO、保留共享 GPIO 时钟，避免破坏其他模块。可能截断正在移位的一字节；正常停止应先等 tx_idle=1。
- 拒绝接管已经 UE=1 或 IRQ 已使能的 USART2；双核所有权依赖约定，未实现跨核仲裁。

启动方式：在本驱动内注册 `INIT_LEVEL_BUS` 启动函数，只初始化 UART，不发送任何字节。该可选外设初始化失败记录于驱动诊断，启动包装函数返回 SYS_OK，以保留既有 Camera/USB/Storage 启动；**系统启动成功不代表 UART ready，必须检查诊断。** 没有新业务轮询/Task；模块若主动发送超过缓存容量，会按规定锁存 overflow，需恢复及及时消费。

只有 CM7 工程加入新文件和 `--keep=bsp_bt_uart.o(*)`，保留全部公共 API 供调试调用；否则未被业务调用的函数会被链接器移除。CM4 target XML 已与基线逐项对比一致。未修改 CM4、Camera/USB/Storage、公共 FreeRTOS、Application 核心、链接内存布局或 MPU。

## 动态内存授权的处理

本次 B1 固定缓冲已足够，**未使用动态内存、未引入 heap_4.c，也未启用全局动态分配**。用户允许“仅 BLE 使用 heap_4.c”的授权保留为后续需要动态分配时的约束；本轮没有用 libc malloc 或其他 allocator 替代。B0 中 NimBLE 的动态分配不兼容结论仍保留，未因本次授权而宣称已解决。若后续接入 heap_4，应单独审查 BLE 使用边界及其他模块约束，不能默认所有模块均可申请堆。

## API 使用约定

```c
#include "bsp_bt_uart.h"

uint8_t bytes[64];
uint32_t count;
BspBtUartDiagnostics_t diagnostics;
SysResult_t result;

result = BSP_BT_UART_GetDiagnostics(&diagnostics);
//必须确认result、ready、fault；启动包装函数的成功不是UART成功证据
result = BSP_BT_UART_Read(bytes, sizeof(bytes), &count);
//BUSY+count=0为暂时无数据；短读正常，成功字节范围是bytes[0..count)
```

Write 返回 SYS_ERR_BUSY 可以同时接受部分字节（最多64），调用者必须推进 count 后的输入位置，不能重发已接受部分。输入在复制完成后可立即复用。写入 TX 队列不等于完成发送；GetDiagnostics 的 tx_idle 要求队列为空、硬件 TC 且无 fault。

所有API只允许线程/启动上下文，全局 PRIMASK 必须为0；ISR不可调用。支持多调用者单次操作串行化，但不同调用者的多次 Write 可能交错，后续协议层必须独占串口。调用者不能让输出 count/diagnostics 与数据缓冲重叠。

诊断通过 `BSP_BT_UART_GetDiagnostics` 取一致性快照。Keil 可观察驱动文件内 `s_debug` 的原始累计计数、`s_busy/s_owned` 和 ring 索引；`s_debug` 内的 pending/tx_idle 是保留快照字段，实时值以 GetDiagnostics 返回为准。计数为32位自然回绕；ore/fe/pe/ne 只统计中断观察到的标志事件，故障锁存后不继续逐字节测量丢失。rx_discarded/tx_discarded 只统计已知的软件队列丢弃量，不包括硬件丢失或正在移位字节。

## 验证证据

- [ARM执行测试](../../Tests/BleB1/results.json)：18/18通过，ARMCC5编译真实驱动，Unicorn运行。覆盖初始化8E1字段、RX满容量及循环、TX满队列及TC完成区分、overflow、ORE/FE/PE/NE及组合错误、恢复成功/超时、GPIO局部恢复、未知时钟拒绝、重复初始化、非法参数、空操作、外设占用拒绝、全局关中断拒绝、双向8192字节顺序、部分写及取消。
- [构建命令记录](../../Tests/BleB1/commands.json) 和 [测试源码](../../Tests/BleB1/uart_contract.c) 可复核断言；[运行脚本](../../Tests/BleB1/run_contract.py) 包含 MMIO 模型。测试模型显式调用IRQ，模拟 RDR消费/ICR W1C/UE ACK、立即TXE，不覆盖真实时序、模拟电气、自动异常调度、嵌套中断或双核。
- CM7和CM4 Keil ARMCC5完整构建均0错误、0警告，日志在 `Tests/BleB1/*build.log`。最终 CM7 API 符号保留已检查。
- [基线内存对照](../../Tests/BleB1/build_results.json) 使用同一工程和工具链，基线仅去除B1文件及keep选项；不是重新拉取另一版本。构建脚本不烧录。
- DTCM实际链接占用：基线 69,184 B → 当前 72,416 B，增加 3,232 B，剩余 58,656 B。RO执行区合计增加 2,784 B；SDMMC DMA 32 KiB与共享区320 B均未变化。详见 memory_summary.json。
- 本轮未烧录或运行实板，没有证明实际115200收发无丢字节、ISR最坏延迟、GPIO电平或与视频/存储共存。

复现（PowerShell，从工程根目录）：

```powershell
python -m pip install --target Tests/BleB1/python_deps unicorn pyelftools
python Tests/BleB1/run_contract.py
python Tests/BleB1/run_build.py
```

依赖及构建产物不提交；脚本使用本机 `D:/32/ARM/ARMCC/Bin` 和 `D:/32/UV4/UV4.exe`，其他环境需要调整。run_build在MDK目录生成临时对照工程，finally清理；uVision可能更新 RTE 注释，仅在确认无用户改动后恢复这些生成差异。

## 实板时钟查询修正

用户提供的 Keil 截图显示 `init_attempts=1`、`ready=0`、`last_result=-7`。原 BSP 调用的 HAL 通用频率查询没有 USART2 分支，返回 0 后触发 BRR 检查失败。BSP 现核对 PCLK1 选择器并使用 HAL_RCC_GetPCLK1Freq。修正后 ARM 合约测试 18/18，CM7 完整目标构建 0 错误、0 警告。实板待重新烧录复测。

## 实板调试与剩余验收

1. 打开 `MDK-ARM/h7_mac.uvprojx`，选 `h7_mac_CM7` 构建；CM4保持现有匹配固件。本轮未自动下载。
2. 从复位运行到启动完成，检查驱动诊断 ready=1、kernel_hz/BRR/actual_baud、fault=0；以逻辑分析仪确认实际波特率、8E1和PD5/PA3方向。若ready=0，先检查last_result及时钟/ACK，不能忽略。
3. 接外部UART测试器前确认电平及隔离方式，避免测试器TX与模块UART_OUT同时驱动PA3；板上模块保持接线时不要直接将MCU TX/RX短接回环。
4. 经批准的诊断调用路径执行Read/Write，持续及时消费RX。用递增字节/序号和独立校验，双向至少各1MiB；保存双方原始字节、实际吞吐、缓冲高水位、ORE/FE/PE/NE/overflow和最大关中断时间。
5. 注入帧/校验错误及故意停读导致溢出；确认fault锁存、IO返回、计数变化、队列丢弃，Recover后新数据正确；测试强制DeInit和再次Init。
6. 正常启动UVC及SD录像后重复压力测试，与基线比较视频、存储错误和IRQ延迟。仅有构建成功不能宣称无影响。

B1代码验收由用户独立审核，实板项目仍 NOT_VERIFIED。CTS接地、蓝牙复位及控制器固件风险按本次授权接受，没有伪装成已经关闭；B2 H5、固件下载、NimBLE及ApplicationV3集成都未实施。
