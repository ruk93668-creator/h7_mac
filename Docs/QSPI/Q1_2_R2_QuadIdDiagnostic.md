# Q1.2-R2 Quad I/O ID 只读诊断

本轮准备 30 MHz 和 120 MHz 两个 M7 测试构建。基线为 `ec5ba117a9483b62ee16f6f7b35a285f9d82bce3`，Q1.2 软件验收仍以 `c58f7d3` 为准。专项入口复用原工程的测试任务，测试镜像仍含原有 Application，需记录实板测试时的业务运行状态。未下载、未执行实板事务。后续烧录与触发等待审核，Q1.3 继续冻结。

已有两次 120 MHz 只读全片结果均为全 FF。30 MHz 的 JEDEC 与 SFDP 签名均已取证。SFDP 报告中的 `samples[1]`、`samples[2]` 还需要用户从保留该报告的 Keil 会话查看并截图。每个样本保存地址、`result_03`、`result_0b`、两个 `confirmed`、两个 `non_ff` 与 `differences`。补齐记录不要求重新烧录。若该会话已失去报告，标为缺证据。

## Figure 41 与 HAL 映射

Winbond W25Q128JV Rev J §8.2.25 Figure 41 的事务按下面顺序实现。图中指令、地址、模式、空周期、返回值依次排列。计时图上首组两字节占用至第 23 个编号时钟（从 0 开始），因此正文中关于空周期位置的叙述以时序图划分为准。

|阶段|线上格式|HAL 字段|
|---|---|---|
|指令|IO0，`94h`，8 CLK|InstructionMode=1 line，Instruction=0x94|
|地址|IO0–IO3，24 位 `000000h`，6 CLK|AddressMode=4 lines，AddressSize=24 bits，Address=0|
|模式|IO0–IO3，`F0h`，2 CLK，满足 Figure 41 的 `Fxh` 要求|AlternateByteMode=4 lines，AlternateBytesSize=8 bits，AlternateBytes=0xF0|
|空周期|4 CLK，数据阶段前换向|DummyCycles=4|
|数据|IO0–IO3，`EF 17`，4 CLK，高半字节先出|DataMode=4 lines，NbData=2|

使用 SPI Mode 0、SDR、每次重新发指令。HAL 的 `QSPI_Config()` 把模式值写入 ABR，把线宽、位数、Dummy 和指令写入 CCR，并设置 DLR。HAL Receive 只改 CCR 的 FMODE 为间接读取，再写 AR 触发，随后等 FIFO/TC 并按字节读取。代码参考 `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_qspi.c:784`、`:1056`、`:2531`。HAL 能表达该帧，物理时序仍需探头验证。

诊断经私有 BSP 句柄和已有同步读取字节路径执行。每次先读 SR1/SR2/SR3 并检查 BUSY、SUS、QE，QE 未置位则跳过 `0x94`。100 ms 预算涵盖状态与 ID。HAL 异常或短接收保留首错并沿用 BSP 清理，成功收到错误签名时保留原始两字节，报告按未匹配处理。测试只新增 `0x94` 指令，原 `0x0B/0x6B` 命令字段与调用不变。默认配置仍为 120 MHz，并保留原 Q1.2 扫描入口。

[Winbond W25Q128JV Rev J §8.2.25 Figure 41、制造商和器件 ID 表及 §9.6](https://www.mouser.com/datasheet/2/949/Winbond_Electronics_Corporation_09_06_2024_W25Q128-3501276.pdf)

## 软件证据

执行 `run_quad_id_tests.py 30` 和 `run_quad_id_tests.py 120`。两频率分别 11/11、1466 条断言，总计 22/22、2932 条断言。覆盖反复 `EF 17`、IO0/IO3 交换、一个额外 Dummy 造成半字节错位、Command/Receive 超时、短接收、QE 未置位、BUSY、SUS、NULL 和频率检查。还实测了模型中 IO1/IO2 交换仍得到 `EF 17` 的局限。Q1.1 原 51/51、Q1.2 原 516/516、SFDP 原 6/6 均通过。

Mock 用半字节序列模拟换线和 Dummy 偏移。对 `E,F,1,7` 四个半字节，输出阶段 IO0、IO1、IO2、IO3 序列分别为 `0111`、`1101`、`1101`、`1100`。其中 IO1 和 IO2 恰好相同，所以即便读到 `EF 17`，仍不能仅靠 ID 区分两线互换。物理连线和时序检查仍需配合波形，`0x6B` 非均匀阵列数据验证仍为 NOT_VERIFIED。

复现构建使用 `run_quad_id_build.py 30`、`120`、`formal`。仅在隔离快照内设置宏，保留原有 M7/CM4 其它工程选项。默认构建去掉诊断宏并核对 CM7/CM4 ELF 中没有 Quad ID 符号。日志和镜像元数据记录在 `Tests/QspiFlashQ1/quad_id/evidence/`，AXF 不提交。

30 MHz 与 120 MHz 的 CM7 诊断 AXF 均为 0 错误、0 警告，负载范围均为 `[0x08000000,0x08024408)`。文件哈希与 Watch 符号地址分别记录在各构建的 results.json。Keil 算法支持 2 MiB 内部 Flash，按扇区下载时仅使用镜像覆盖的两个扇区。

默认配置的 CM7、CM4 Rebuild 均为 0 错误、0 警告，负载范围分别为 `[0x08000000,0x080241D4)`、`[0x08100000,0x08106C88)`，与 Q1.2 记录相同。

## 获准后的实板顺序

1. 保留并导出 SFDP 取样明细后，在原 `h7_mac.uvprojx` 选择 `h7_mac_CM7`，把之前的 `QSPI_SFDP_DIAGNOSTIC` 宏换成 `QSPI_QUAD_ID_DIAGNOSTIC=30000000`。Rebuild，确认新 AXF 含 `g_qspi_quad_id_arm`、`g_qspi_quad_id_report`，并重新核对 Flash 负载。只使用 MCU 内部 Flash 算法、Erase Sectors、Program 和 Verify。下载范围限 M7 内部 Flash 前两个扇区 `[0x08000000,0x08040000)`。CM4 镜像起于 `0x08100000`。
2. 运行现有 Q1 门闩流程。记录 `EF4018`、READY、读回事务频率。准备采集 CS#、CLK、IO0–IO3。暂停写 `g_qspi_quad_id_arm=0x51393452`，读回后继续 M7。入口运行最多 8 次，首个传输错误后结束，签名不符会保留并继续记录。只消费一次门闩。用 Watch 查看 `complete`、`attempted`、`matched` 和每次的 ID、QE、HAL 错误、CR/DCR、确认字节数。
3. 30 MHz 完成 8/8 且波形与 Figure 41 一致后，再审核 120 MHz 对照。确认 U5 实装、运行时 VCC 为 3.0–3.6 V、实测时钟和板上采样条件。换成 `QSPI_QUAD_ID_DIAGNOSTIC=120000000` 做独立 Rebuild 与下载，重复相同取证。若出现异常，单独保留该频率的原始结果。
4. 回退时恢复原 120 MHz Q1.2 M7 镜像或移除诊断宏重新构建并核对。外部 W25Q128 全程只读。
