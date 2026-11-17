# Windows 固定内容 RX 验证

本轮不改固件、驱动或网卡 IP。Windows 对端工具使用已经安装的 Npcap，不需要 Scapy。网卡以实际 MAC 精确选择，并确认 Npcap GUID 和 Ethernet 链路类型，禁止默认选第一个接口。

2026-11-17 本机只读检查：以太网 3、EC-1A-C3-3E-DB-B2 为 Up；--check 成功打开正确 Npcap 接口且发送 0 帧；Python 语法检查通过。这不是板卡 RX 验收结果。

## 单帧完整内容

1. 最新 smoke 固件运行，命令 1/2 后 result=0、state=2；continuous=0。
2. 在 Tests/Ethernet/test_main.c 的 `g_eth_test_last_rx_length = (uint32_t)length;` 行设置断点，条件为：

   `g_eth_test_last_rx[12] == 0x88 && g_eth_test_last_rx[13] == 0xB5`

   该位置已经成功复制数据，普通 ARP/DHCP 不触发此条件。设置后继续运行 CPU；有优化导致断点不能落到该行时，先核对实际反汇编位置，不通过随意修改 DMA 内存来测试。
3. 对端 Wireshark 抓以太网 3，显示过滤器为 `eth.type == 0x88b5`。
4. 明确确认板卡正在运行后，从 PowerShell 执行：

   ```powershell
   python "D:\32\STM32H7\h7_mac_wk\Tests\Ethernet\windows_tx.py" --source-mac EC-1A-C3-3E-DB-B2 --count 1
   ```

5. 板卡断住后检查 length=60（该行执行前 last_rx_length 仍可能是上一帧，应查看 length 或执行该赋值），以及 g_eth_test_last_rx 的所有 60 字节：

| 偏移 | 预期内容 |
| --- | --- |
| 0–5 | 02 47 47 00 00 01 |
| 6–11 | EC 1A C3 3E DB B2 |
| 12–13 | 88 B5 |
| 14–17 | 00 00 00 01，网络字节序序号 |
| 18–59 | 00 01 02 … 28 29，共 42 字节 |

对端抓包与板卡接收内容均匹配，才能记录本轮固定内容 RX 通过。Npcap 提交成功只说明交给驱动，不等于板卡收到。

## 后续多帧

工具支持 --count 1000 --interval-ms 10，但完成单帧后再开始。必须取消单帧断点，避免暂停 CPU 导致人为耗尽。现有 rx_count 会包含 ARP/DHCP，last_rx 也只保存最后一帧，不能据此声称 1000 帧完整校验通过；严格多帧验收还需专用序号/数据校验计数或逐帧证据。

工具的 --dry-run 只生成向量，不打开网卡；--check 打开并核对 Npcap 网卡但不发包。没有实际板卡结果时，报告必须保持未验证。
