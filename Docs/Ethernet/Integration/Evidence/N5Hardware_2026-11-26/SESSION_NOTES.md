# 会话补录

这份文件转录用户在会话中提供的文字和 Keil Command 输出。原始文件级证据存放在 Original。

- 用户报告开始时已烧录 N5 CM7 验收版。M4 版本及板上 Flash Verify 未确认。
- 首轮 DHCP 启动失败后，用户按步骤重新设置 Windows WLAN 共享，目标为以太网 3。随后 Watch 显示 lease_valid=1，IP 原始值 0xC989A8C0，flags=0x00000003。
- 单帧超时后的连接失败为 WinError 10061。再次用同一个结果路径运行，独占创建返回 FileExistsError [Errno 17]，请求在建立网络连接之前终止。原结果文件 Original/frame_diag_03/hw_one_frame_retry.json 保留。
- 自动记录尝试中，用户文字报告 phase 从 2 到 4，心跳在增长。下一轮进入 phase=2，随后 flags=3、lifecycle=5。

## Keil Command 原始读数

首次读取驱动故障统计：

```text
MAC=2 DMA=0 MACerr=0 MDIO=11
```

禁用断点后，用户报告先见 flags=7。电脑随后抓到 ICMP Echo Reply 和 TCP 80 RST/ACK。用户再读取：

```text
phase=2 flags=3 lifecycle=5 MDIO=9
```

用户最终回复“结束了”。最终寄存器与 Watch 快照未提供。
