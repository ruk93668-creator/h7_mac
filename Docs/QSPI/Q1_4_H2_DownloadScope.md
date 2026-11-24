# Q1.4-H2 候选镜像下载范围

正式候选为本地 `output/QspiFlashQ1/h2/candidate02`。从 R2 基线 `a897daaca069e1bccad6f1b91aabf09b35a4eb65` 构建，CM7、CM4 均为 ARMCC5 完整 Rebuild，0 Error、0 Warning。生成器校验 Intel HEX 校验和、EOF、地址扩展、记录连续性与重叠，解析实际数据地址上限并独立核对 AXF 加载区。**实板下载和运行未获准。** 见 [下载范围清单](../../Tests/QspiFlashQ1/h2/evidence/download_scope.json)。

## MCU 内部 Flash

|镜像|Intel HEX 实际数据范围，结束地址不含|数据量|SWAP_BANK=0 时涉及的 128 KiB 扇区|
|---|---|---:|---|
|CM7| `[0x08000000, 0x08005C5C)` |23,644 B|Bank 1 Sector 0，`[0x08000000, 0x08020000)`|
|CM4| `[0x08100000, 0x08100674)` |1,652 B|Bank 2 Sector 0，`[0x08100000, 0x08120000)`|

当前板卡的 `SWAP_BANK`、启动地址和选项字节保护状态均为 NOT_VERIFIED。Bank Swap 生效时两段 AXI 地址对应的物理 Bank 对调，需在任何下载前实板只读确认。 [ST RM0399 第 4.3.13 节及表 18](https://www.st.com/resource/en/reference_manual/rm0399-stm32h745755-and-stm32h747757-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)

后续申请下载前，核对两核当前固件、用于回退的镜像及 SHA-256，并检查内部 Flash 算法实际会擦除的扇区。配对下载会暂时替换两核各自的现有应用。CM4 测试镜像完成 HSEM 握手后停驻，生产 CM4 应用不会同时运行。禁止全片擦除、外部 QSPI Loader、选项字节更改及自动下载。生成的隔离 Keil 工程关闭了调试启动前自动更新 Flash。只使用上表对应的 MCU 内部 Flash 扇区提出后续申请。

## SHA-256

|核|文件|SHA-256|
|---|---|---|
|CM7|AXF|`0a3c52a283a37ed1ad2b2ced549ce4365651906681e0978e7f4d01adfd14b800`|
|CM7|HEX|`4c49b34ea54903246f7c1aa88220480f4b082f1689a5a50f212567e8f3242966`|
|CM7|MAP|`3807a65f16aca13813ff30aa41ee6797ec2731a51c462ada281beae88feb467b`|
|CM4|AXF|`0cfea9be62318f1c32d5498239a2944a9bac42af575ebd4c5a224e3134c03177`|
|CM4|HEX|`f53d5b846ade56c706ec4fc0ea38da900b4dd7679dbdb2b7c0ca0609901487b7`|
|CM4|MAP|`cb87e9a0a0ed9fd446bc689984c442dfb1b4f2e410bf458cab7125ba60594279`|

复现命令为 `python Tests/QspiFlashQ1/h2/build.py <新目录标签>`。构建从冻结基线提取生产时钟函数、BSP、ST HAL 和测试 Scatter，自动审计 AXF/HEX/MAP。重新构建生成的地址和 SHA 应使用该次清单核对。

## 镜像资源

|核|Code / RO / RW / ZI（B）|最大静态栈|
|---|---:|---:|
|CM7|22,650 / 810 / 184 / 35,256|884 B|
|CM4|944 / 696 / 12 / 1,636|24 B|

CM7 具有一个 3,072 B 静态 H2 任务栈、1,024 B 静态 Idle 栈和 4,096 B 启动栈。TIM6_DAC 供 HAL Tick，SysTick、SVC 和 PendSV 供 FreeRTOS。无 FreeRTOS 动态分配，定时器任务关闭。QSPI 使用同步轮询，QUADSPI 向量为默认处理函数，DMA、Memory Mapped、HAL QSPI IRQ 与写入相关入口未链接。CM4 无 QSPI 符号，使用已有测试停驻入口。见 [资源与链接证据](../../Tests/QspiFlashQ1/h2/evidence/resources.json)。

## RAM 现场导出

以下地址对应候选 `candidate02`，起止地址均包含。只有在获准运行且到达稳定的 `WAIT_RECOVER` 或 `DONE` 后读取报告。Recover 期间不单步。

|符号|地址或导出范围|大小|
|---|---|---:|
|`g_qspi_h2_arm`|`0x20000060`|4 B|
|`g_qspi_h2_report`|`0x20001228–0x2000252F`|4,872 B|
|`g_qspi_h2_before`|`0x20002530–0x2000452F`|8,192 B|
|`g_qspi_h2_after`|`0x20004530–0x2000652F`|8,192 B|

PC 离线校验可接收 BIN 或 Keil `SAVE` 导出的 Intel HEX。成功报告还需提供两份完整数据导出，超时或其他失败只需 report。可加 `--h1` 指向确认属于同一板的 `backup.json`、`neighbor.bin`、`target.bin` 目录。AXF 存放位置变化时用 `--axf` 指定。

```powershell
python Tests/QspiFlashQ1/h2/verify_report.py --manifest output/QspiFlashQ1/h2/candidate02/download_scope.json --report h2_report.hex --before h2_before.hex --after h2_after.hex
```
