# Q1.4 H2 证据收尾

核验输入为 QSPI 提交 `fdb45718ec60db0c4d970d02cad70fa99662dd78`，时间为 2026-11-24。本轮仅离线复验和设计，没有连接板卡或执行固件。H2 的单次软件注入读取故障及显式恢复证据均通过复验；Q1.5 实施等待独立审核。

## 原始证据

重新读取 `output/QspiFlashQ1/h2/field-h24-recover01/` 的四份输入，与原 hashes.json 逐项比对，全部相符。

|文件|SHA-256|
|---|---|
|h2_recover_report.hex|2681056a2a29932f1ae06d5fd5505855446d21449a4c5875621dc991041dd1e3|
|h2_before.hex|aebe2d21aba0453487d8fad4949c5e7fa657396497f1b4594413c78704399c96|
|h2_after.hex|0524d456a206389bcec571ae85790bc10267829b4f21849610afaf9dc53ef0ed|
|verified.json|b6a9648621461d2beebb682d7f164ccb9cdb7c2bcc08cb5e6ac5874b373aa2ea|

复验使用现有 verify_report.py。报告的 header、各阶段快照和扇区比较均与原 verified.json 相符。H1 原始两份 4 KiB BIN 的保存哈希及实际字节也重新核对。Recover 次数为 1、返回 SYS_OK，原始 READ/RECEIVE 首错 -9 保留。两扇区 0B/6B 验读与恢复前数据一致，原始两扇区全 FF。无追加非均匀 Quad 验收结论。详见 [第一次故障](Q1_4_H2_4_FirstFault.md)及 [Recover 结果](Q1_4_H2_4_RecoveryResult.md)。

## 独立审计包

本地包为 `output/QspiFlashQ1/h2/QSPI_Q1_4_H2_Closure_20261124.zip`，SHA-256 为 `4b4acf83e5965a40bf1bc90fd5a4094a54bffc4e02d33c213132c2e8c02e4c18`。内含三份最终 HEX、第一次故障 HEX、原 verified.json、捕获哈希清单、冻结 H2 两核 AXF/HEX/MAP、下载范围清单、离线解码器和 replay.py。解压后使用标准 Python 执行 `python replay.py`，已在本机执行通过。

包内排除原有 MCU 相机固件、UID、Option Bytes 和敏感板卡资料。H2 测试镜像与原有相机镜像身份不同，索引逐项标出 H2 工件。包未携带 H1 原始 BIN，其原始对比仅留既有 verified.json 的哈希和结论。独立包复验 H2 前后原始字节及镜像身份；H1 原始字节复核需在本机原归档执行。索引及包摘要见 [索引](Closure/H2_PackageIndex.json)和 [摘要](Closure/H2_PackageSummary.json)。

## 当前板卡与下载历史

用户本轮明确确认“未恢复，最后下载的仍是 H2 测试固件”。此项为用户现场确认，本轮没有独立读取板上固件；最近归档状态为 H2 DONE。当前生产 Camera、UVC、SD、IPC 共存尚未在该 H2 镜像上运行。

H2.4 用户取消了 MCU 内部 Flash 原固件备份。历史下载准入仍存在备份、Option Bytes、独立下载复读等证据缺口，不能追认为完整 H2_DOWNLOAD_PASS。历史 [下载阻塞记录](Q1_4_H2_4_FieldAdmission.md)继续保留。

相机固件恢复应作为单独下载申请。先明确使用冻结 main 或后续获准的集成镜像，隔离构建两核，解析实际 HEX/AXF 区间，计算扇区和 SHA，核对本次 Option Bytes/保护/启动配置并保存当前可读内容。回退对象须明确为已保存的 H2 配对候选或其他指定工件。没有原现场备份时，只能声明下载指定版本，无法证明相机固件与下载 H2 前的旧现场逐字节一致。本轮未构建、未申请或执行恢复下载。
