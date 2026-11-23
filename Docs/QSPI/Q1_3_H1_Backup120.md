# H1 120 MHz 只读备份替代方案

按用户确认，使用 120 MHz 只读导出替代本次 30 MHz 捕获。H1 擦写准入仍为 BLOCKED。未下载、未操作板卡、未生成实际备份或写入包。

专用 CM7 配置为 `QSPI_H1_BACKUP120`，与旧 `QSPI_H1_BACKUP`、`QSPI_H1_WRITE` 和 Quad ID 等诊断互斥。入口先读取 JEDEC、唯一 ID 及 16 字节 SFDP，要求 `53 46 44 50` 签名通过。再用 `0x0B`、`0x6B` 各读 `[0x00FFE000,0x01000000)` 全部 8 KiB，比较每一字节。器件 QE 未置位或任一路失败均拒绝。此配置不发送 `0x03`，镜像中没有 HAL QSPI Transmit 或写入包入口。H1 公共写入窗口仍为空。 [Winbond W25Q128JV Rev J §8.2.6–8.2.9、§8.2.28、§9.6](https://www.mouser.com/datasheet/2/949/Winbond_Electronics_Corporation_09_06_2024_W25Q128-3501276.pdf)

这能产生两个 4 KiB 原始数据候选文件。若两路全 FF，SFDP 签名能验证同速单线带地址事务的已知响应，但不能独立证明阵列为空或证明四线时序。真实文件仍需核对并独立留存。未来获批的写入候选另在 30 MHz 下比对 UID、实时内容和备份字节，再决定能否发 WREN。

## 已审核软件材料

- ARMCC5 CM7 Rebuild 0 错误、0 警告。AXF 负载为 `[0x08000000,0x08024150)`，仍仅覆盖 M7 内部 Flash 前两个扇区。AXF SHA-256 为 `b76ce3c694d587bfcd974e4a5657e4fc6b4528075959b0ae98e20c0f567c365d`。真实目标 Bank Swap、选项字节、下载设置和实际回退固件仍待确认。
- 120 MHz 专项 4/4 通过，1,643 条断言。覆盖正确签名和两路一致、双读不一致、SFDP 错误与 QE 未置位。原 H1 30 MHz 两种模式回归 15/15，Q1.1/Q1.2/Q1.3 与真实 HAL 空授权门禁复测通过。
- PC 工具测试 10/10，包含使用实际 120 MHz AXF 地址的合成 HEX 导出、拆分与归档校验。仍可读取旧版 30 MHz 归档。新版元数据记录 120 MHz、双读 opcode 和 SFDP 原始 16 字节。

## 手动备份（核对并获准下载后）

1. 在 U5 确认运行供电 **3.0–3.6 V**。按 H1 审核要求先核对实际板的 Bank Swap、选项字节、M7 回退文件及 Keil 的 MCU 内部 Flash 算法。下载只读 AXF采用 Erase Sectors、Program、Verify。原 Keil 工程使用 `h7_mac_CM7`。如从原工程重建，将旧诊断宏替换为 `QSPI_H1_BACKUP120` 后 Rebuild。必须另行核对重建 AXF 的哈希、负载与符号，归档工具使用与实际加载 AXF 匹配的构建清单。已审核 AXF 和清单位于本地 `output/QspiFlashQ1/h1/evidence120/backup120/`。
2. 在 Q1 `phase=2` 暂停，写 `g_q1_hw_arm=0x51315244` 并继续。确认 `EF4018`、READY、无 FAULT、频率读回 `120000000`。再暂停写 `g_qspi_h1_arm=0x48314252` 并继续。等待 `g_qspi_h1_capture.complete=1`、`result=0`、`version=2`，任何错误均保留现场数据。
3. 暂停 M7。已核验 AXF 的 `g_qspi_h1_capture` 位于 `0x2000801C`，完整报告长度 **16,440 B**。可在 Keil 使用 `SAVE h1_original_120.hex 0x2000801C,0x2000C053`。使用本地构建清单和 `h1_backup.py import-capture`，提供实际读取时间、板卡标识和固件完整 Git SHA，生成 `target.bin`、`neighbor.bin` 和 `backup.json`，再独立复制、核对文件和哈希。 [Keil µVision SAVE](https://www.keil.com/support/man/docs/uv4cl/uv4cl_cm_save.htm)

实际波形未采集不影响本轮只读备份。备份、分区权属、实际下载配置、回退方案和分步执行批准完成前，外部 Flash 擦写保持 NOT_AUTHORIZED。原方案见 [H1 准入报告](Q1_3_H1_Admission.md)。
