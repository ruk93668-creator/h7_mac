# MSC-2.1 REQUEST SENSE CDB补零兼容修复

日期：2026-11-18。VERIFIED=源码/本次证据确认；NOT_TESTED=未实测；INFERRED=推断。

## 基线与范围

VERIFIED：本地和远端起始SHA均为e56437ef44d6a47812da19ef5d0aae1d6ffb5df2。任务中给出的e56437ef44d6a47812da19f192d441724178a7882与之不同，按上一轮已确认且本地/远端一致的真实基线实施，未切换/reset或覆盖工作区。

仅修改生产usb_msc_scsi.c、对应Tests/MscSenseCompat与本报告。两份用户Keil工程文件修改保留且SHA256前后相同；未提交用户设置。无BOT、USB Core、UVC、SDMMC、FatFS、MPU、Linker、FreeRTOS或CM4生产修改。

## VERIFIED：根因与修复

原抓包msc24-enum-02.pcapng原始Frame773/4443的REQUEST SENSE有效6字节后补零，CBW CDB Length=12。原解码器要求严格等于6，导致没有Data IN、失败CSW、Residue18。

现在仅将opcode03从六字节严格校验组独立：要求6≤CDB Length≤16；字节6至声明末尾必须为零。前6字节的保留字段、Control及Allocation Length解析保持不变。其他opcode仍要求原长度，BOT仍保持31字节CBW与1～16字节CDB校验。SCSI也防御性拒绝超过16的REQUEST SENSE，实际USB路径由BOT先拒绝。

调用链：USB ISR登记OUT → UsbTask/BOT解析CBW → USB_MSC_ScsiDecode(next_scsi)生成响应 → BOT发送Data IN → CSW提交 → Platform真实完成序号 → BOT提交next_scsi并清Sense。Decode只设置clear_sense，不直接清除context。现有生命周期未修改。

## VERIFIED：软件验收

- 18个新增真实Core/Platform/BOT集成场景全部通过（415个C断言）。6/12/16字节、非零首/尾填充、短CDB、BOT拒绝17字节、保留字段、Allocation18/8/0/255、主机要求18但allocation8时Residue10、原opcode23的05/20/00，以及CSW发送失败保留Sense均覆盖。
- 直接检查Mock HAL提交长度、Sense字节、CSW tag/status/residue。Sense在Data IN完成、CSW提交及硬件CSW完成但Task尚未确认时均保留；Task确认成功CSW后才清零。
- 用e56437e旧SCSI源码运行同一组测试，7个相关兼容场景按预期失败；修复后全部通过。baseline_results.json中的失败表示缺陷复现，不能当作修复版本失败。
- 全部9项MSC既有测试执行成功；10套历史USB/UVC测试逐项断言结果和退出码与审批基线一致。历史失败仍保留，未修改旧断言或历史报告。执行器运行后恢复旧结果文件，当前结果单独保存到regression_results.json。
- RAM MSC、默认UVC、SD只读三种CM7完整编译均0 Error/0 Warning。临时工程与RTE已恢复；新固件位于output/MSC24/msc21/build，哈希见build_evidence.json。

## VERIFIED：Windows实板抓包

用户已确认烧录新RAM固件并保存output/MSC24/msc21/msc21-enum-01.pcapng。原始文件SHA256与解析记录在hardware_results.json；只保存目标bus1/address53的过滤副本after_fix.pcapng，避免提交其它设备流量。before_fix.pcapng为上一轮目标47/48的过滤副本。下列帧号指原始新抓包，过滤副本会重新编号。

| 交互 | 原始Frame | 实测结果 |
|---|---|---|
| INQUIRY | 538～543 | 36字节，成功CSW |
| 不支持的0x23 | 551～559 | 失败CSW，符合原设计 |
| REQUEST SENSE CBW | 560 | CDB Length12、IN、期望18字节 |
| Sense Data IN | 563 | 实际18字节，70/05/0A/20/00 |
| REQUEST SENSE CSW | 565 | Status0、Residue0、tag匹配 |
| 另外两组0x23后取Sense | 566→574→577→579；580→588→591→593 | 两组均成功返回原05/20/00 |
| READ CAPACITY(10) | 652→655→658 | 成功，响应0000000f00000200，即16×512B=8192B |
| READ(10) | 714→717→719 | LBA0单块512B，成功CSW、Residue0 |

整份目标流量解析出1450笔完成命令，所有CBW/CSW tag匹配；1442笔成功、8笔失败，没有phase-error CSW。8笔失败不等于新增故障，包含未支持/字段拒绝和首次介质Unit Attention，详细范围以抓包为准。成功READ CAPACITY有766笔，成功READ10有444笔；这只证明本次窗口内观察到的交互，不代表长期稳定性验收。

## 停止边界

VERIFIED：本次REQUEST SENSE兼容修复的指定实板链路通过，Windows已继续容量查询和RAM读命令。未切换SD可写模式、未执行真实SD写入。其它未支持命令保持合法失败。

NOT_TESTED：本轮不宣称RAM写入、多块读写、BOT Reset Recovery、UVC视频、真实SD或长期稳定性通过。MSC-2.1完成后停止，等待人工审核，不进入下一阶段。
