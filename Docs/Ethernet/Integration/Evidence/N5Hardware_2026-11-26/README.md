# N5 实板证据索引

2026-11-26（Asia/Shanghai）。完整实板验收结果为 **FAIL**。

保存 60 个原始文件，包括 10 份 PCAPNG、14 张 PNG、20 份 JSON 和 7 份日志。保留原始目录结构、字节和空日志。
5 份手工保存的 Wireshark 抓包和源文件逐字节相同。其他抓包来自定向主机测试。
SESSION_NOTES.md 单独补录会话中的文字结果，区分现场采集文件和后续整理。
SHA256SUMS.txt 记录原始文件和索引文件哈希。调试脚本中的路径和地址是本轮现场记录。

| 抓包 | 报文数 |
|---|---:|
| [N5_DHCP_DIAG_01.pcapng](Original/start_fail_01/N5_DHCP_DIAG_01.pcapng) | 40 |
| [N5_DHCP_RECOVERY_01.pcapng](Original/start_fail_01/N5_DHCP_RECOVERY_01.pcapng) | 439 |
| [N5_FRAME_DIAG_03.pcapng](Original/frame_diag_03/N5_FRAME_DIAG_03.pcapng) | 217 |
| [N5_FRAME_DIAG_04.pcapng](Original/frame_diag_03/N5_FRAME_DIAG_04.pcapng) | 27 |
| [N5_START_VIDEO_02.pcapng](Original/start_retry_02/N5_START_VIDEO_02.pcapng) | 304 |
| [auto_frame_20261126_173852_097663.pcapng](Original/frame_diag_03/auto_frame_20261126_173852_097663.pcapng) | 4 |
| [auto_frame_20261126_175022_599865.pcapng](Original/frame_diag_03/auto_frame_20261126_175022_599865.pcapng) | 8 |
| [auto_frame_newip_175144_638789.pcapng](Original/frame_diag_03/auto_frame_newip_175144_638789.pcapng) | 10 |
| [auto_probe_20261126_174541_683479.pcapng](Original/frame_diag_03/auto_probe_20261126_174541_683479.pcapng) | 6 |
| [no_break_frame_180201_014652.pcapng](Original/frame_diag_03/no_break_frame_180201_014652.pcapng) | 18 |
