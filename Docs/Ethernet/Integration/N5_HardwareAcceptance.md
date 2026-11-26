# N5 — 实板验收操作包

审查日期：2026-11-26（Asia/Shanghai）。源码冻结：`78580073610ff4cfe06a4b004b1b0d75bffef499`；main：`851c1115fe097f7fd2e0bdcaa5f7b3f8c03feb71`。

**准备完成；实板全部NOT_RUN。未自动烧录。**

## 工程、固件和身份

正式版使用ApplicationV3生产main，不再使用E7测试工程。
```powershell
python -B Tests/Ethernet/build_production.py --output output/Ethernet/production
python -B Tests/Ethernet/build_production.py --acceptance --output output/Ethernet/acceptance
```
生成MDK-ARM/h7_mac_ethernet_production.uvprojx和h7_mac_ethernet_acceptance.uvprojx。
正式版自动启用DHCP+HTTP视频；验收版先空闲，仅增加Control命令生产者，不含Mock/测试main/fixture。
用户自行下载，双核用配套CM4版本。保存板卡V1、文件、下载器及真实Verify日志。
本地SHA仅证明文件，板上身份须下载器Verify或Flash分段读回比对，不能用本地AXF SHA冒充。

| profile | 文件 | SHA-256 |
|---|---|---|
| production | h7_network.axf | `700972bffc47b401ad46312b09fa555db705143ce3f6d30157609816feeb638a` |
| production | h7_network.hex | `37c6b7f3e44c78e5d576f6ec74907fc1a787455e7eac973f8e85798d65da5d57` |
| production | h7_network.map | `17195716ae816e75e2517c7c5abc3d3995665168387c6a280087a424c5c34348` |
| acceptance | h7_network_acceptance.axf | `4bd7dda51d8de45a32cc8a5662cc1d3e7ec548b0d8e31052122091380b9216f9` |
| acceptance | h7_network_acceptance.hex | `e34d7e49e49b1876fe6c752e83c421a0abaf6a6f337e609dffad4b7e7d5bcfa8` |
| acceptance | h7_network_acceptance.map | `f6e94e0c7096b5a4b1c1753de6f8bcbf5eae8b4dd6c033b14216916c70e6be97` |

## MPU/DCache实际读回

先在main的Platform_RtosStart调用前停住：Framework已准备内存且DCache开启，业务DMA未启动。
保存*((unsigned int*)0xE000ED14)的CCR，DC位0x10000应为1。
MPU CTRL=0xE000ED94，RNR=0xE000ED98，RBAR/RASR=0xE000ED9C/0xE000EDA0。
保存RNR，暂停时分别选4/5读取，再恢复RNR；不修改RBAR/RASR。

| 区域 | Base | Size | TEX/C/B/S/AP/XN/EN | RASR |
|---|---|---|---|---|
| 4 DMA | 0x24078000 | 32KiB | 1/0/0/1/3/1/1 | 0x130C001D |
| 5 CPU | 0x24010000 | 64KiB | 1/1/1/0/3/1/1 | 0x130B001F |

同时保存Region2 SDRAM及Region3 SDMMC实值，与源/MAP对照。源码不代替硬件证据。
移除会中断采集的断点再F5；连续测试期间不Halt或复位。

## 网络、视频和重连

PC直连板卡时必须有DHCP Server。Windows WLAN共享要选实际“以太网3”，通常PC为192.168.137.1；
只设置静态IP不能提供DHCP。保存ipconfig及route print。
观察g_network_status：heartbeat增长、driver.state=2、lifecycle=3、flags&7=7、address.lease_valid=1。
记录本次IP/Mask/Gateway/Server/T0/T1/T2和PHY ID；0x4D89A8C0示例对应192.168.137.77，不沿用旧IP。
ping本次IP，浏览器访问http://实际IP/，验证真实320×240视频。
PHY Up不等于租约完成；Connected不等于视频/吞吐通过。
拔线、插回，记录Link、DHCP、监听和收流恢复。Wireshark保存真实ARP/DHCP/ICMP/TCP/HTTP PCAPNG，
包括ACK中T1/T2实际选项。

```powershell
python -B Tests/Ethernet/e6_peer.py --board-ip 实际IP --camera --frames 1000 --output hw_basic.json
python -B Tests/Ethernet/e6_peer.py --board-ip 实际IP --camera --frames 50 --reconnect 10 --rst --output hw_rst.json
```
也检查慢读/停读、普通结束。原帧和私有借用必须归还，PCB/SEG/PBUF/heap回基线。
cyclic timer是全局资源，STOP后基础条目不为零，不能强行清成零。

## 100轮生命周期（验收版）

关闭浏览器及客户端，等待heartbeat增长、g_n5_acceptance.phase=0。
开始前设g_n5_cycles=100、g_n5_dwell_ms=1000、g_n5_ready_ms=30000，再设g_n5_command=1。
Control通过原监督器执行，QUERY确认Link/IP/listener全部就绪才计活动时间；
真实STOP完成后才增加completed，无租约不能计作通过。
等待completed=100、phase=4、first_fault=0、active=uncertain=0，保存前后全部统计。
phase=5是FAIL；g_n5_command=3只请求真实清理，保留首错，不清变量凑成功。
停止后实读MACCR.TE/RE、DMACTCR.ST和DMACRCR.SR确认关闭，软件READY不能代替读回。
保存Network及旧任务的实际最小栈水位，不修改优先级/栈制造通过。

## 60分钟多外设联合（验收版）

先保存上一轮证据，再重启验收版。连接USB数据线，UVC主机读取器实际收流；
准备获准写入的测试SD卡，指定未占用的非零g_n5_file_number，未指定时工具拒绝联合写入。
设g_n5_joint_ms=3660000给PC启动留1分钟，g_n5_ready_ms=30000，g_n5_command=2。
确认active=15、OLED/IPC正常及实际网络就绪，在1分钟内启动：
```powershell
python -B Tests/Ethernet/e6_peer.py --board-ip 实际IP --camera --frames 1000 --seconds 3600 --min-fps 9.33 --output hw_joint_60min.json
```
9.33FPS是既有实测参照，须记录同等场景/帧大小；不能降低门槛造PASS。
20FPS另用--min-fps 20单独报告，未达到保留OPEN/FAIL，不与长稳混写。
使用短租约Server使T1发生在持续负载内，保存实际Request/ACK和T1/T2。
同时记录USB实收、SD文件可读性、Camera帧/丢帧、CRC/重复/乱序/丢失、TCP重传、
各池峰值、DMA/MAC/DCMI错误及最小栈水位。RBU次数不是丢帧数，需结合序号/报文定位。
到期由原监督器有序关闭，等待PCB/TIME_WAIT和Camera归还，保存资源及硬件读回。

## 实板状态

Verify、MPU/CCR、PHY、真实视频、100轮、60分钟、续租/重绑/到期、多外设和栈水位均NOT_RUN。
这些步骤由用户执行，失败保留原始PCAPNG/JSON/截图，不用主机模拟代替。
