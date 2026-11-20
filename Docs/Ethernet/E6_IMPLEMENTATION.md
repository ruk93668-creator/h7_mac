# STM32H747 Ethernet E6 实施与交付报告

开发分支 `codex/ethernet-bringup`。起始基线 `29c959115809a2c5c9131871ad8574533aa2ae88` 已同时核对本地与远端。用户的 `MDK-ARM/RTE/_h7_mac_CM7/RTE_Components.h` 保持原有修改，SHA-256 为 `8f99c42e1fabf545b02671bdce03b50fae7c02c071dd15ce4544eb8a8c12b336`。

## 交付内容

- TCP 80 上的单浏览器 HTTP 和 MJPEG，复用 E5 代次句柄、发送环、ACK 与关闭流程。默认 DHCP Only，静态模式 `192.168.50.2/24`。应用 UDP 可禁用，DHCP 保留自己的 PCB。
- 八张 160×120 原创 JPEG 和离线生成脚本。固件仅从 Flash 读取夹具，默认按 200 ms 调度，逐帧附带长度、序号及 CRC32。
- `python -B Tests/Ethernet/build_firmware.py --e6` 生成独立 Keil 工程 `MDK-ARM/h7_mac_ethernet_e6.uvprojx`、`output/Ethernet/e6/eth_e6.axf` 及同目录 MAP。`output/Ethernet/e6_result.json` 记录构建输入、源码提交、AXF SHA-256 与链接校验。参见[架构](E6_ARCHITECTURE.md)、[HTTP 接口](E6_HTTP_API.md)、[MJPEG 协议](E6_MJPEG_PROTOCOL.md)、[内存预算](E6_MEMORY_BUDGET.md)和[实板步骤](E6_HARDWARE_VALIDATION.md)。
- 最终构建源码提交 `7f321f60f69db869bb4aab6e7418641391dfb0c3`，AXF SHA-256 `58d56bab2285313fb91077cf7fe23d917be9d9a718d838c962fc59bb0113827d`。交付复跑日志 `output/Ethernet/e6_host/delivery-20261120-132549.log`。

## 软件与实板结果

| 验证 | 结果 | 证据 |
| --- | --- | --- |
| E6 和 E5 独立 Keil 构建 | PASS | 两个工程均为 0 Error / 0 Warning。E6 Region 5 余 19,020 B，DMA 地址 `0x24078000`。 |
| E4.1 / E4.2 / E5 主机回归 | PASS | 原有测试断言保留。E5 仍验证 DHCP、双 TCP 角色与 UDP 并发、窗口耗尽、重连和资源回收。 |
| E6 HTTP 与 MJPEG 主机验证 | PASS | 上游 lwIP 和模拟 MAC/时钟。验证请求分段、合并、无效输入、精确 Multipart、JPEG CRC、ACK 计数、零窗口重开、停读超时、FIN/RST 与代次隔离。 |
| E6 虚拟时钟长稳 | PASS | 605 秒虚拟时间内 3025 帧得到 TCP ACK，5 次 DHCP 续租。另验证链路恢复、同地址及变更地址重绑、50 次重复首页请求与资源归还。 |
| PC 工具与 JPEG 源 | PASS | 重新生成的八张 JPEG 和 C 数组逐字节一致。标准库工具回环覆盖逐帧解析、破损数据拒绝、慢读、停止读取、FIN/RST 和重连。 |
| Chromium 软件渲染 | PASS | localhost 夹具使用 E6 首页和 JPEG，实际解码到 160×120，两次截图的图像像素不同。本地证据位于 `output/Ethernet/e6_host/browser-1795091264`。 |
| 实板 DHCP、浏览器、10 分钟、闭环容量与恢复 | NOT_RUN | 本轮按用户选择交付软件和固件，实板稍后验收。未取得板端 FPS、实际吞吐、池峰值、Flash Verify 或 PCAPNG。 |

## 监听恢复修复

修复前，真实 lwIP 主机复现了 DHCP 重获原地址后 `ready=1`、`listen=0`、绑定返回 `ERR_USE(-8)`。同一端口仍有旧的 `TIME_WAIT` PCB。证据保存在 `output/Ethernet/e6_host/relink-before.log`。TCP 服务端 listener 启用 lwIP 地址复用，保留旧的 `TIME_WAIT` 及四元组处理，应用仍只接受一个活动连接。复验成功时旧 PCB 仍在 `TIME_WAIT`。上游源码和 MAC/DMA 未改动。

E5 回归构建重新生成了 `output/Ethernet/e5` 下的本地构建产物及同名日志。这些文件是本轮回归版本，E5 已验收固件原 SHA 及历史抓包、测试记录仍可从 E5 报告和本机证据目录核对。当前交付停在 E6。
