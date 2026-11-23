# Q1.3-H1 实板擦写准入准备

状态 **BLOCKED**。固件构建和软件门禁验证完成。尚未下载镜像、导出真实备份、生成真实板卡的 RAM 操作包或执行实板擦写。输入基线为 `e9e9a4f2954ed0070fe9a89be7e7cc2aaf8b55e4`。

|准入项|状态|证据与缺口|
|---|---|---|
|测试分区|CONDITIONAL|候选仅 W25Q128JV `[0x00FFF000,0x01000000)`。仍需确认业务用途、板卡身份和独占权|
|真实原始备份|NOT_CAPTURED|已提供读回和导出工具。仍缺目标及邻扇区的真实 PC 文件与独立副本|
|恢复|SOFTWARE_VERIFIED|Mock 验证原内容恢复及全 FF 时跳过编程。实板恢复仍 NOT_RUN，须当面审核备份和恢复条件|
|H1 专用 M7 镜像|BUILT / REVIEW_REQUIRED|两份 30 MHz AXF 均已生成并审计负载范围。待审核实际板卡 Bank Swap/选项字节、下载算法、当前运行固件和回退镜像|
|实际擦写|NOT_AUTHORIZED|本轮只批准准备。不得将构建或软件门禁通过视为实际执行批准|

## 镜像与隔离

|配置|AXF 内部 Flash 负载（右端不含）|AXF SHA-256|
|---|---|---|
|30 MHz 只读备份| `[0x08000000,0x080240FC)` | `bbfa7c3a2f6d7bd39596dbfad22502f3c734807f226d6ac63255db3a0bb7d197` |
|30 MHz H1 写入候选| `[0x08000000,0x08024EA0)` | `0ca60a9bd2ee35e0d2dd7c98ae3867243ebca957303b27916b0169db4f20eb9f` |
|Q1.3 默认 M7 回退候选| `[0x08000000,0x080241DC)` | `76f86ae7f57dbb52201fe93f76e8fca38314ed732569f9ce21c8cabcbb75ddec` |

两个候选均为 ARMCC5 Rebuild 0 Error、0 Warning。按 128 KiB 扇区，覆盖范围均为 MCU 内部 Flash 的 `[0x08000000,0x08040000)`，避开 `0x08100000` 起始的 CM4 负载。地址是 ELF 静态审计结果。实际 Bank Swap、选项字节和调试下载算法尚未在目标上验证。正式操作前还需确定原板 M7 镜像对应的回退文件，不能根据本地候选推定板上运行版本。实际下载申请只涵盖 MCU 内部 Flash 所覆盖扇区，采用 Erase Sectors、Program、Verify，避开外部 QSPI Loader、全片擦除、选项字节及 CM4。 [ST RM0399 §4](https://www.st.com/resource/en/reference_manual/dm00176879-stm32h745-755-and-stm32h747-757-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf)

构建脚本在 `output/QspiFlashQ1/h1/evidence/{backup,write}/` 创建独立快照，仅在快照的原 `h7_mac_CM7` 目标切换 `QSPI_H1_BACKUP` 或 `QSPI_H1_WRITE`。两者互斥，固件固定 30 MHz。原工作目录的 `.uvprojx/.uvoptx` 保留。生产公共默认写入策略仍为空。只读版链接产物不含 HAL QSPI Transmit。写入版新增私有策略，仅由既有 M7 测试任务调用，一次启动只执行一次操作，无公共地址解锁接口。应用、平台、框架和 CM4 源码中无 QSPI 业务访问。该检查限本仓库源码，板上的实际分区归属仍需人工核对。

## 只读备份步骤（仅在确认 MCU 内部下载范围后）

1. 确认板卡标识、U5 型号和实际运行时供电。选择只读候选，核对对应 AXF 哈希，并先取得针对该镜像的内部下载批准。准备实际板上回退文件。在 Keil 以对应 AXF 手动下载和调试。
2. 完成原 Q1 门闩 JEDEC 识别，要求 `EF4018`、READY、无 FAULT、30 MHz。再用只读门闩 `g_qspi_h1_arm=0x48314252` 触发一次。等待 `g_qspi_h1_capture.complete=1` 且 `result=0`。它用 `4Bh` 读取 8 字节 Flash 唯一 ID，之后分别用 `03h` 和 `0Bh` 各完整读取 8 KiB，并比较两路全部字节。4Bh 的 32 个占位时钟由 HAL 的 24 位单线零位阶段加 8 Dummy 组成。 [Winbond W25Q128JV Rev J §8.2.26，图 42](https://www.mouser.com/datasheet/2/949/Winbond_Electronics_Corporation_09_06_2024_W25Q128-3501276.pdf)
3. 暂停 M7 后导出 **完整 16,424 字节**的 RAM 报告。已核验的只读 AXF 中 `&g_qspi_h1_capture=0x2000801C`，最后一个字节地址为 `0x2000C043`。若使用重建镜像，以其 AXF 符号为准。Keil 命令示例 `SAVE h1_original.hex 0x2000801C,0x2000C043`。 [Keil µVision SAVE](https://www.keil.com/support/man/docs/uv4cl/uv4cl_cm_save.htm)
4. 执行 `h1_backup.py import-capture`，提供只读构建的本地 `build.json`、导出文件、板卡标识、带时区的实际读取时间、固件完整 Git SHA 和新输出目录。工具可接收 Intel HEX 或完整原始 BIN，校验 ELF 地址/哈希、导出长度和两路内容，保存 `target.bin`、`neighbor.bin`、完整快照及 `backup.json`。把 4,096 字节的原始文件、JSON 和 SHA-256 放到独立介质，重新比对后再提交准入审核。两扇区导出工具不覆盖完整 16 MiB。若分区用途仍未知，先另行保存完整只读镜像，继续保持擦写 BLOCKED。

```powershell
python Tests/QspiFlashQ1/h1_backup.py import-capture --kind original --build <只读构建的build.json> --export <Keil导出的hex或bin> --board <板卡标识> --read-time <带时区实际读取时间> --firmware-sha <完整Git提交> --out <独立新目录>
```

## 供后续审批的写入流程

每种操作使用独立 RAM 包和不同的单次门闩，输入中包含固定地址、操作类型、Flash UID、原备份、邻扇区和当前扇区快照。PC 先验证 AXF、两次归档身份与 SHA-256，固件再核对 CRC32、实读 UID 和状态，并逐字节核对当前目标及邻扇区。CRC 用于发现传输错误，RAM 门闩和审批编号用于受控调试流程，二者不承担认证。尚未生成任何真实板卡的 RAM 包。将来必须有具体实板执行授权才可调用 `prepare-package` 与载入其输出，并在确认 AXF 符号后只载入已核对的 DTCM 地址。调试器在门闩生效后不得再改输入。包工具只生成文件，不调用 Keil 或探针。

1. Program 从目标页末偏移 255 开始写 520 字节，同一次程序包含 1、256、256、7 字节的四个片段。仅原始图案区全 FF 且原备份、邻扇区、实读内容完全匹配时允许。完成后单线、Quad 分别逐字节对比原始图案，记录 CRC32 和首个失配位置。还核对图案范围之外与相邻扇区。若图案区已有非 FF 数据，当前方案拒绝执行，不额外执行预擦除。
2. 重新使用只读版导出写后目标与邻扇区，用 `--kind programmed` 保存。Erase 核对该文件与原始图案推导出的状态，逐字节再次核对实时内容，并将**写后当前完整内容**传给 BSP 的擦除备份参数。完成后目标 4 KiB 逐字节检查全 FF，邻扇区应与原备份相同。
3. 重新只读导出擦除后的内容（`--kind erased`）。原始扇区含非 FF 时，Restore 将原始 4 KiB 逐页恢复；原始扇区全 FF 时只核验，不发 WREN。最终重读目标、邻扇区，分别与原始文件逐字节比较。每步停止并保存报告，下一步应核对本次结果和新备份。出现故障、未知完成或失配时，不能自动重试、复位 Flash 或继续到下一步。
4. 120 MHz 只读对照单独审核。如果要利用本轮已知图案，应在 30 MHz 编程验读成功后、擦除和恢复前安排；若先恢复，再另行审核图案来源。空白状态不提供非均匀对照。120 MHz 取证需复核供电和实际四线波形。

## 软件证据

- ARMCC5/Unicorn H1 专项 **15/15，12,163 条断言**。覆盖双读不一致、默认禁用、单次门闩、地址/UID/CRC/邻区/现状拒绝、复合操作拒绝、QE=0、编程、擦除、非空恢复、空备份仅核验及 Quad 失配停止。
- PC 离线工具 **8/8**，含实际两个 AXF 符号、Intel HEX 来回转换、错误区间和校验和拒绝、备份变动检查及三种合成状态。未使用真实板卡数据。
- 普通模式 Q1.1 51/51、Q1.2 516/516、Q1.3 48/48，真实 HAL 初始化超时和空授权门禁回归通过。

原始构建、Mock、离线测试及回退文件哈希核对记录见 `Tests/QspiFlashQ1/h1/evidence/`。在备份、所有权、现场选项字节/下载范围及分步执行批准闭环之前，准入保持 BLOCKED。
