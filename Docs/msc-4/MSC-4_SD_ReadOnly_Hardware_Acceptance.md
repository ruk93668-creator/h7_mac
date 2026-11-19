# MSC-4 真实 SD 卡只读实板联合验收（未完成）

记录日期：2026-11-18～19（Asia/Shanghai）。工程分支：`codex/usb-msc-bringup`。

**结论：正常扇区读取和 FAT32 文件复制已有实板证据；读取中拔线后的重连枚举失败，整体验收未通过。停止于问题取证，不修改生产代码，不进入 MSC-5。**

标记说明：VERIFIED 表示有对应源码、构建或实测证据；INFERRED 表示推断；UNRESOLVED 表示异常或结论未确认；NOT_TESTED 表示未完成测试。每项的证据范围分别列出，源码确认不能替代实板确认。

## 1. 基线、范围与固件

- VERIFIED：构建基线 `57120051270dd18c5aed834f1c5857f0e4090725`，远端 `origin` 为 `https://github.com/ruk93668-creator/h7_mac.git`。
- VERIFIED：开始时已有 `MDK-ARM/h7_mac.uvprojx`、`MDK-ARM/h7_mac.uvoptx` 用户修改。构建使用临时独立工程，结束恢复 RTE 原始字节并检查用户工程字节。后续用户 Keil 操作产生的配置变化保留，不纳入提交。
- VERIFIED：本任务新增验收文档及主机测试工具，不修改固件源码、Keil 配置、MPU、scatter、FreeRTOS、CM4；不格式化、不修复文件系统、不执行文件写入。
- VERIFIED：RO 固件预处理结果 `USB_MODE_BOOT_MSC=1`、`USB_MSC_SD_TEST=1`、`USB_MSC_SD_WRITE_ENABLE=0`。除初次预处理外，另按实际工程全部 preinclude 配置复核；`MscSd_Execute` 不包含 `SD_Block_Write` 调用。

| 固件 | SHA256 | 完整编译 |
| --- | --- | --- |
| SD 只读 MSC | `48e504bd29e5310f014b1a65128a9085cf15b5bfc2e64d77253b9e8a4634fc07` | VERIFIED：0 Error / 0 Warning |
| 默认 UVC | `909e06de702699d1ea9153b6290a9a76742bfb0693c63e0372837e06bd39a40a` | VERIFIED：0 Error / 0 Warning |

编译器 Arm Compiler 5.06 update 7 build 960；`D:/32/UV4/UV4.exe -j0 -r <临时工程> -t h7_mac_CM7 -o <日志>`。完整命令及结果见 `Tests/MscSdReadOnly/build_results.json`，脱敏日志见同目录 `sd_readonly_build.txt`、`uvc_build.txt`。本地 AXF、HEX、MAP 位于 `output/MSC4-readonly/{sd_readonly,uvc}/`。为匹配用户现有下载命令，仅在输出目录创建同哈希 `h7_mac_CM7.axf` 别名；未改 Keil 工程。

NOT_TESTED：本轮未补做 UVC 摄像头实板回归。编译通过不能代替摄像头回归。

## 2. 写保护与所有权审计

VERIFIED（源码/预处理）：`MscSd_Init -> SD_Block_Claim(SD_BLOCK_CLIENT_RAW)`；租约绑定 StorageTask。受控 SD 模式禁止 FATFS Claim，StorageTask 不运行本地 StorageFS 文件流程。SCSI WRITE 解码检查介质 WP，Storage Submit 再检查 WP；SD Adapter 预处理移除写调用。

VERIFIED（最终 ELF）：`SD_Block_Write`、`BSP_SDMMC_WriteBlocks` 未链接；公共 BSP 收发实现使 `HAL_SD_WriteBlocks_DMA` 仍留在镜像，不能声称镜像完全不存在写入口。生产代码审计摘要及哈希见 `source_audit.json`。

VERIFIED（实板快照）：RAW client=2，owner=`0x2000E1B0`（StorageTask handle）；StorageFS `fatfs_called=0`、`open_files=0`、`writable=0`。本轮断线及重连后租约仍保留。`s_lease` 在此 ELF 中未形成可读取存储位置，未将地址零当作有效诊断值。

VERIFIED：主机读取工具只开放 INQUIRY/TUR/REQUEST SENSE/READ CAPACITY/MODE SENSE/READ(10)，无文件创建或磁盘写入 API。Windows SCSI pass-through 要求的读写权限 handle 不代表发送了写 CDB。

NOT_TESTED：协议级 WRITE(10) 负向请求未执行。自动审批拒绝移除调试写入口保护后发送 WRITE 的操作，理由为实际写卡风险与保护前提不足；没有绕过拒绝。保留的测试工具模式不代表该项通过。DATA PROTECT 07/27、Worker 未收 WRITE、累计写调用/写入块数为零缺乏完整动态计数证据；快照 `write=0` 仅说明最后一次操作为读，不能证明历史累计写入为零。

## 3. SD 初始化、内存与 DMA

VERIFIED：初始化快照由用户在 USB 数据线断开、Run 后 Break 时直接 `SAVE` SRAM 取得，使用本次 ELF DWARF 解码。初始门禁通过：SD 初始化结果0，Media Ready=1，Write Protected=1，4-bit，总线40 MHz，kernel240 MHz，high_speed=1，初始化耗时11 ms。

| 项目 | 实测值 / 状态 |
| --- | --- |
| CID 原始 HAL 四字 | VERIFIED：`2673101104, 808464432, 393, 3170974906`（按驱动数组顺序） |
| 逻辑扇区 | VERIFIED：512 字节 |
| 扇区数 | VERIFIED：15,224,832 |
| 原始容量 | VERIFIED：7,795,113,984 字节 |
| 卡状态 | VERIFIED：4（TRANSFER） |
| DMA 专用区 | VERIFIED：MAP 为 `0x24000000`、32 KiB、32字节对齐 |
| MPU region3 | VERIFIED（源码）：non-cacheable / non-bufferable / shareable；未改配置 |
| 运行时 DMA 地址/容量 | VERIFIED：后续快照 `0x24000000` / 32768；容量字段不等于每次实际传输量 |
| 实际读批次 | VERIFIED（快照/部分调试记录）：requested_blocks=2，completed_blocks=2 |
| MemoryValid 独立返回值采样 | NOT_TESTED：未单独采样函数返回寄存器 |

INFERRED：初始化与后续读取成功表明源码内存校验路径未拒绝该专用区；不能将此写成对 `BSP_SDMMC_MemoryValid()` 的独立实测返回值。

## 4. USB / SCSI 抓包

VERIFIED：已归档可解析 USBPcap 文件 `output/MSC4-readonly/msc4-readonly-20261118.pcapng`，313,720,252 字节，SHA256 `856d45d7c883ca6725288f3f5096df4cdaf96f24a01f15f80a993be31fa5b454`；元数据分析由 `analyze_capture.py` 生成。包含多个设备地址、重连及历史探针异常片段，不能视为单次无异常枚举。

VERIFIED：归档抓包解析到 4,245 对完整 CBW/CSW，完整配对 Tag 均一致；Status0=4,147、Status1=98。INQUIRY72、REQUEST SENSE97、READ CAPACITY274、MODE SENSE50、READ(10)2815、TUR899、未支持0x23为36，另有 SYNCHRONIZE CACHE/START STOP 各1。命令出现不等于所有语义均验收完成。

VERIFIED：READ CAPACITY 实际返回 lastLBA=15,224,831、blockSize=512，与 SDMMC 一致。受控 MODE SENSE 返回 WP 位；Windows 映射 F: FAT32。WMI 几何容量7,789,340,160不作为真实容量依据；FAT 卷容量7,793,147,904也不等于整卡容量。MBR 分区起始63、长度15,224,769扇区、类型0x0C，签名55AA。

VERIFIED：完整成功 READ 的 USB Data IN 长度与命令匹配，分析未发现长度不符。UNRESOLVED：抓包另有未完成命令，不能宣称所有传输都完成、所有 Residue 都为零、没有 STALL 或卡死。

NOT_TESTED：完整 BOT Reset（Class FF）及两个 Clear HALT 的时序未取得对应验收证据。GET MAX LUN/标准枚举记录存在，但本轮断线/重连两次操作用户均确认没有抓包，不能用旧文件补充其时序。

## 5. 真实扇区读取与分块

VERIFIED：Windows SPTD 实际读取 LBA0、普通LBA1、LBA2双块、LBA0三块、LBA0连续64块、LBA63跨区域四块、尾部双块、最后合法LBA；返回长度及状态通过。重启后再测八组哈希一致。非法LBA15,224,832返回 CHECK CONDITION，Sense5/21，未伪造成功。

VERIFIED：抓包存在128块、96块、64块等大 READ；总计2815条 READ，统计块数299,529（包含失败命令请求值，不能等同成功 SD 块数）。主机大 READ 正常完成已有证据。

INFERRED：源码1024字节 chunk 对应每批最多2扇区；`ceil(blocks/2)` 是预计后端请求数。部分调试日志记录连续2块读完成，但探针引发 DAP 错误且 Tag 在不同会话复用；未对每条大 READ 的全部后端调用、CSW 做无歧义关联，完整逐批联合验收为 UNRESOLVED。

## 6. FAT 文件复制与 SHA256

VERIFIED：从现有 F: 读取并复制到电脑输出目录，未在 SD 创建/删除/改属性。小/中/大三个文件跨多个 BOT 分块，流式读取哈希等于本地副本哈希。

| 文件类别 | 字节数 | SHA256 |
| --- | ---: | --- |
| 小文件 | 24,752 | `400b4ef800a48ad3824cb6420a3a8e5caaa385f1e9a1eff5c14a90712263ceee` |
| 中文件 | 1,654,784 | `b9570310de3bcd1cda1c8f05d05544e08bfcfc8f62fb6274c7c2b637234f78a3` |
| 大文件 | 135,379,304 | `b847a55264a4832f241c3f66c61b4abace820ecf9da3384a2fe538c150f01e8a` |

NOT_TESTED：用户未提供独立可信原件或历史 SHA256 清单。以上仅验证本次读取与本地复制一致，不证明原始数据端到端正确。原始文件名清单、文件内容及整 Hub 抓包仅留本地，不上传 Git。

## 7. 异常、探针与生命周期

### 7.1 调试探针干扰

VERIFIED：读入口/完成探针日志出现错误63（重复开启 LOG）、Cannot access Memory、RDDI-DAP Error；探针启用后枚举异常，禁用本次探针并重启板卡曾恢复 F:。此事件不能用于证明产品正常或已定位根因。保留 `msc4_diag_probe_fault.hex`、寄存器快照和原始日志；CFSR/HFSR为0，DFSR=0xA，仅说明该采样点未见记录的 CPU fault。后来未重新启用读取探针。

### 7.2 只读 Phase Error 注入

VERIFIED：2026-11-19，对512字节 READ 命令仅给256字节主机 Data IN，捕获到 CSW Status2（实时记录frame1649220）；主机IOCTL121超时。之后 LBA1024的64块读取成功，32768字节，SHA256 `a876e3f24d6addb71d9c41f9dd747db6246ee3744da5069451e9967f30aab877`。

UNRESOLVED：该实时片段未归入上面的静态归档文件，未取得可交付的完整 Reset/ClearHALT 时序；后续读成功不能证明 BOT Reset Recovery 全部步骤正确。

### 7.3 读取中拔线与重连失败（当前阻塞）

第一轮用户确认忘记拔线，日志与快照已保留于本地 `diagnostics/attempt1_no_unplug/`；NOT_TESTED：不得将该轮算作拔线通过。

第二轮（2026-11-19，北京时间）：

1. VERIFIED：12:12:17.954 启动180秒限时纯READ循环；257次128扇区读取完成。下一条 READ LBA2176、128扇区在12:13:04.067记录IOCTL121失败。失败时的 returned_bytes 字段不能解释为全部数据已有效返回。
2. VERIFIED（用户操作确认）：用户拔掉 USB 数据线、保留调试供电，等待后 Break；保存 `MDK-ARM/msc4_diag_read_disconnect_02.hex`（12:13:00）。快照早于主机15秒超时返回，符合不同观测点；用户确认本轮没有抓包。
3. VERIFIED（断线快照）：SDMMC active=0/fault=0，最后LBA2238、requested=completed=2、write=0；Worker state=0（EMPTY），pending=0。BOT state=4（BOT_IN）、active=1、epoch=1，transferred=31744、remaining=33792。RAW仍归StorageTask，FatFS未访问。
4. VERIFIED（源码）：`CM7/BSP/bsp_usb.c` 关闭 `vbus_sensing_enable`；`usb_msc_bot.c` 的 BOT_IN 等待 IN完成，30秒超时在 backend poll 路径，不覆盖此等待。INFERRED：物理拔线通知/IN终止处理可能关联本次等待；尚无足够证据确定根因。
5. VERIFIED：不复位，Run后重接 USB，用户报告 F:未恢复；Windows PnP 为 `USB\\VID_0000&PID_0002\\8&33147C3B&0&4`，错误“设备描述符请求失败”。
6. VERIFIED（重连快照12:16:31）：BOT state=0/OFF、active=0，Class state=0，started=1、stop_required=0、last_result=0；IDMA仍active=0/fault=0，Worker EMPTY，RAW仍保留，fatfs_called=0。表明Class已释放，但枚举未恢复；并非证明发生了完整取消和安全恢复。

UNRESOLVED：读取中拔线后的取消、USB IN缓冲归还、迟到IRQ处理、重连枚举恢复。NOT_TESTED：未证明拔线瞬间SDMMC IDMA正忙；不能用最终DMA空闲快照代替“在途IDMA取消”测试。

NOT_TESTED：本轮最终 Windows安全弹出、完整BOT Reset Recovery、SD忙时USB Reset联合取证、板卡全部断电/取卡确认。已通知用户保持暂停、不复位，结束时先关闭全部供电再取卡。

## 8. 交付、证据边界与下一步

原始证据位于 `D:/32/STM32H7/h7_mac_sd/output/MSC4-readonly/`，用户原始SAVE文件仍保留MDK目录；部分副本在 `diagnostics/archive/`。原始整卡数据、全SRAM、ELF/HEX/MAP、包含其他USB设备内容的原始抓包不提交Git。提交测试工具、脱敏构建日志及必要状态摘要；不提交用户Keil配置。Git备份提交号由本次交付回复记录，不能预填尚未完成的推送。

当前阻塞为断线后重新枚举失败。下一步需要人工审核本报告及证据，再确定修复授权；本轮不修改BSP、Platform或BOT来绕过问题，不自动进入MSC-5。未完成项目保持上述状态，整体MSC-4不得标记通过。
