# H2.4 第一次读取故障现场核验

板卡按用户登记为 H747-W25Q。核验输入为用户经 Keil 保存的 candidate02 现场文件 h2_fault_report.hex、h2_before.hex。原始文件和解析结果保存在本地 output/QspiFlashQ1/h2/field-h24-fault01/。日期为 2026-11-24，精确现场读取时间未独立取得。

结论为 **H2_FAULT_PASS，Recover NOT_RUN**。报告 phase=6、complete=0，处于 WAIT_RECOVER。顶层 result=-4 为预置值，单独判定依据为已发布阶段快照与原始接收记录。首次 Watch 中 id、consumed、injected 等零值与后续 RAM 导出不一致，具体刷新原因未确定。

|证据|现场结果|
|---|---|
|初始 ID、配置与状态|EF4018，控制器配置读回 120 MHz，SR1/SR2/SR3 有效，BUSY/WEL/SUS=0|
|授权|consumed=1，injected=1，denied=0，WAIT_FAULT 指令计数为零|
|实际接收及软件注入|原始 HAL_OK，返回改为 HAL_ERROR；ErrorCode=0，RxXferSize=16、RxXferCount=0、HAL READY|
|失败读取|SYS_ERR_IO，completed=0；READ/RECEIVE，0x0B，首错=-9|
|锁存|FAULT=1，READY=0；WREN、修改历史和完成未知均为零|
|清理|SYS_OK，COMPLETE，控制器停止已验证；时钟门和所有权均为零，所属引脚及 mux 与准备前保存值一致|
|旁路拒绝|普通 Init、Read 均返回 SYS_ERR_INVALID_STATE，清理后等待期间指令计数无增加|
|恢复|recovery_attempts=0，恢复入口 Tick 记录为零，尚未执行|
|阵列参考|8,192 字节覆盖 FFE000–FFFFFF，与 H1 neighbor/target 原始备份逐字节相同，均为 FF|

原始报告 BIN SHA-256 为 `007068b3408e8847e3bf3b51523b20d3926a277700044f55c30f095e1e662c27`。恢复前 8 KiB BIN SHA-256 为 `7d2c7ac4888bfd75cd5f56e8d61f69595121183afc81556c876732fd3782c62f`。

用户后续明确选择 Keil/CMSIS-DAP 并取消 MCU 内部 Flash 备份。下载前器件身份、Option Bytes、内部 Flash 独立复读和当前固件回退证据仍缺失，因此本报告只确认第一次故障路径的现场结果，不将原下载准入改为完整 PASS。此前 H2_DOWNLOAD_BLOCKED 原始记录保留。

第二门闩继续等待 GPT 独立审核授权。保持 WAIT_RECOVER，软件尚不能据此证明实际 Recover 成功、恢复后数据完整性或长期稳定性。
