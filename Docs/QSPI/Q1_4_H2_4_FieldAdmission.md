# Q1.4-H2.4 第一次现场准入

板卡标识按任务登记为 `H747-W25Q`。冻结基线为 `d271b55c145ba6a3be94a15c55883c4aa7042c85`。结论为 **H2_DOWNLOAD_BLOCKED**，第一次故障验收 **NOT_RUN**，因此未取得 `H2_FAULT_PASS`。未启用任何下载算法，未连接到目标执行 SWD 读写，未运行候选镜像或写 RAM 门闩。

## 已核验

`candidate02` 的 CM7、CM4 AXF、HEX、MAP 共六项 SHA-256 均匹配冻结下载范围清单，两份 HEX 同时匹配本轮授权值。见 [下载范围与冻结哈希](Q1_4_H2_DownloadScope.md)。

Windows 已连接设备枚举匹配到 `#PW_WINUSB_CMSIS-DAP`。本机 STM32 ST-LINK Utility 的 CLI 仅执行探头枚举 `ST-LINK_CLI.exe -List`，原始结果如下。

```text
STM32 ST-LINK CLI v3.6.0.0
STM32 ST-LINK Command Line Interface

No ST-LINK detected!
ST-LINK_CLI exit=2
```

应用注册表和常用安装目录未定位到 STM32CubeProgrammer。已枚举的 CMSIS-DAP 未用于替代本轮指定的只读准入方式。

## 未取得的现场证据

|项目|状态|
|---|---|
|STM32H747XIH6 器件 ID、板卡身份、稳定 SWD 和实际供电|NOT_VERIFIED|
|当前 SWAP_BANK、两核 BOOT、RDP、写保护及相关 Option Bytes|NOT_VERIFIED。H1 的历史读回无法证明本次上电配置|
|当前 CM7、CM4 固件准确版本及回退方法|NOT_VERIFIED|
|CM7 备份及独立复读 `[0x08000000,0x08020000)`|NOT_RUN，无备份 SHA-256|
|CM4 备份及独立复读 `[0x08100000,0x08120000)`|NOT_RUN，无备份 SHA-256|
|配对下载、内部 Flash 复读和 H2 启动日志|NOT_RUN|
|WAIT_FAULT、WAIT_RECOVER、HAL Tick、FAULT 快照及数据导出|NOT_RUN。本次未观察到 H2 运行状态|

本次未修改 Option Bytes、外部 Flash 或 Keil 配置。只有重新取得本轮指定的只读连接证据、两核完整扇区独立复读及回退信息后，才能重新评估下载准入。第二次 Recover 门闩继续等待独立审核授权。

## 本地原始证据

原始枚举输出和工件比对保存在 `output/QspiFlashQ1/h2/field-h24-admission/`。Git 审计仅保存结果、摘要及证据哈希。

|文件|SHA-256|
|---|---|
|`stlink_list.log`|`ed3887cc2cf937e86d6d417704985e5049f9007ebf0eb1a8130f428ea3648246`|
|`connected_probe.log`|`df8a3ee28435b388a98b0411955b0fcf8daeaba908181af2874df111fa3a27831`|
|`artifacts.json`|`def7f5a1304e89b894b2cddfb95db10316b153830fe52650ad5f0593dc050d99`|
