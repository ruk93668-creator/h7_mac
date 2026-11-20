# Ethernet E7 实施报告

## 实现与范围

独立 E7 固件将 OV5640 的 160×120 JPEG 接入 E6 HTTP/MJPEG。相机侧使用现有 BSP、设备和 CameraCapture 流接口。前台捕获服务校验并复制 FIFO 中的完整帧，归还原始借用，再将私有副本租借给 HTTP 分块提交。相机停机证据来自现有 StopFacts 与硬件状态检查。具体数据路径、资源冲突审计及缓冲区归还条件见同目录架构、Camera 集成和内存所有权报告。

## 软件验证

| 验证项目 | 结果 | 证据 |
| --- | --- | --- |
| E4.1 / E4.2 Host | PASS | 真实 lwIP 的静态 IPv4、ARP/ICMP、链路、DHCP 租约及回退回归 |
| E5 Host | PASS | TCP、UDP 和传输状态回归 |
| E4.2、E5、E6 PC 端工具 | PASS | 四组 Python 测试，共 10 项 |
| E6 静态源兼容 / 动态租约 | PASS | 同一套 Host 断言覆盖精确 JPEG/CRC、零窗口、超时、FIN/RST、链路及 DHCP 地址变化。每种源的虚拟 605 秒测试均完成 3025 帧 TCP ACK、5 次 DHCP 续租 |
| E7 Camera Provider | PASS | 模拟 Camera API 覆盖 FIFO 跨尾、长度边界、CRC、私有缓冲固定、快速生产者、失效归还、相机错误和 Stop 失败隔离 |
| CM7 生产目标全量编译 | PASS | Keil 0 Error / 0 Warning，Code 137998 B，RO 3114 B，RW 1396 B，ZI 113484 B |
| E7 独立目标全量编译 / MAP | PASS | Keil 0 Error / 0 Warning，Code 109482 B，RO 4154 B，RW 496 B，ZI 127976 B |
| OV5640、真实画面、慢客户端、断链重连、DMA 停机、MPU 读回、≥600 秒长测 | NOT_RUN | 执行 E7_HARDWARE_VALIDATION.md 并保存板端证据 |

E7 相对 E6 的链接统计 ROM 减少 7520 B，含私有 SDRAM 的静态 RAM 增加 63796 B。其中私有 JPEG 为 61440 B，其余静态 RAM 增加 2356 B。编译 E7 Host 模拟器时仅针对遗留 Camera 头文件注释编码兼容 MSVC C4828，Camera 源码保持原样。

## 固件证据

- 交付文件位于 `output/Ethernet/e7/eth_e7.axf`，MAP 同目录。AXF SHA-256 为 `8a6dc1779708a2e2272a9aa9f1440badd42fc0c08c4d9ad77897b5c0d11e0863`。
- `output/Ethernet/e7_result.json` 记录构建开始时的 Git 基线 `c31b6882c3c31cd6f818dc9c74f2ef7fcec80a7b`、本轮实际输入文件的 SHA-256、链接检查与固件 SHA。构建时已包含本轮固件变更，输入在编译期间保持稳定。
- Region 4 使用 12544 B，位于 `0x24078000`。Region 5 使用 46704 B，剩余 18832 B。JPEG 私有区位于 `0xD07F0000`，MAP 标记为 `UNINIT` 及 `Zero RW`，没有 SDRAM 装载数据。板上 MPU 读回与实际写入验证为 NOT_RUN。

## 剩余风险

真实 JPEG 尺寸、画面变化、持续帧率和板端停机恢复仍需以实板证据确认。超过 60 KiB 的帧按设计丢弃并计数。相机 DMA/DCMI 分项硬件错误通过调试器寄存器采样观察，Camera 软件提供合并错误计数。实板验收前，E7 作为独立测试固件交付。
