# H2.4 显式 Recover 现场结果

用户明确批准第二门闩后，于 2026-11-24 经 Keil 导出现场结果。板卡按用户登记为 H747-W25Q，镜像为已审核 candidate02。校验工具核对本地 CM7 AXF SHA-256 匹配冻结清单，解析两次现场导出并比较恢复前后及 H1 原始备份。结果为 **H2_RECOVER_PASS**，范围限本次单次软件注入的只读接收故障。

原始输入 h2_recover_report.hex、h2_before.hex、h2_after.hex，以及完整解析 verified.json、哈希 hashes.json 保存在本地 `output/QspiFlashQ1/h2/field-h24-recover01/`。第一次故障档案仍保留，INIT、NORMAL、INJECT、FAULT 和 WAIT_RECOVER 各快照与第一次导出逐字段一致。

|检查|现场结果|
|---|---|
|结束状态|phase=9，complete=1，result=SYS_OK|
|授权与次数|consumed=3，injected=1，denied=0，recovery_attempts=1|
|显式恢复|recover=SYS_OK，recovery_stage=READY，READY=1、FAULT=0，初始化次数从 1 增至 2|
|实际通信|恢复期间 9F、05、35、15 的命令和完整接收计数各增加一次，其余操作码计数无增加。JEDEC=EF4018，状态 valid_mask=7，BUSY/WEL/SUS=0|
|时基|进入 Recover 前 HAL Tick=632757，返回快照 Tick=632758，recovery_elapsed_ms=1；TIM6 CEN/UIE、NVIC 启用，CPU 屏蔽为零。计时仅为本次毫秒级观测|
|资源|控制器和 Flash 空闲证明均为 1，QSPI 门开启且 BSP 所有权为 1，控制器读回与 READY 一致|
|首错保留|READ / RECEIVE、SYS_ERR_IO、0x0B 和 HAL_ERROR 保留；WREN、修改及完成未知均为零|
|恢复后数据|0B 和 6B 各完整接收 32 个 256 B 分段，覆盖 FFE000–FFFFFF；两扇区分别逐字节与恢复前比较成功，CRC32 均为 F154670A|
|H1 对照|两个 4 KiB 扇区均与 H1 原始备份相同，SHA-256 均为 f47a8ec3e9aff2318d896942282ad4fe37d6391c82914f54a5da8a37de1300c6，均全 FF|
|已 READY 再次调用|返回 SYS_ERR_ALREADY_EXISTS，恢复次数保持 1，未新增 Flash 指令|

本次成功不扩展为物理总线故障、写入完成未知、断电一致性、长期稳定性或新的非均匀 Quad 数据验收。下载前用户取消 MCU 内部 Flash 备份，Option Bytes、独立下载复读等准入原始证据仍缺失，现有准入记录继续保留。

|原始导出|SHA-256|
|---|---|
|h2_recover_report.hex|`2681056a2a29932f1ae06d5fd5505855446d21449a4c5875621dc991041dd1e3`|
|h2_after.hex|`0524d456a206389bcec571ae85790bc10267829b4f21849610afaf9dc53ef0ed`|
|h2_before.hex|`aebe2d21aba0453487d8fad4949c5e7fa657396497f1b4594413c78704399c96`|
|verified.json|`b6a9648621461d2beebb682d7f164ccb9cdb7c2bcc08cb5e6ac5874b373aa2ea`|

本轮由用户操作板卡，代理仅离线读取、核验和归档已保存文件。生产 BSP 和 HAL 未修改。Q1.4 总体结论仍待独立审核，Q1.5 继续冻结。
