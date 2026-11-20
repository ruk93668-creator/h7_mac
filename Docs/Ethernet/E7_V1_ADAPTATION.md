# E7 V1 主控板适配

E7 V1 专用目标复用本仓库的 V1 供电与硬件 I2C1 相机 BSP。构建选择取自固定 Git 对象 `8b87a7a14ba6b84537464668eec4b0b251542b20`，其中已包含供电实现 `9bc95fb832728c1aaebf734f7798f55780b2c83d`。同目录 `Tests/Ethernet/board_v1` 的相机 BSP 源文件和头文件与该对象中的对应文件字节一致。这样单分支克隆也可重建独立目标。生产 BSP 及其 Keil 目标保持原样。

- PA15 拉高使能 U10 的 1.5 V DVDD，PH13 控制 PWDN，PH15 控制低有效 RESETB。PA8 用 PLL1Q 240 MHz 除以 10 提供 24 MHz XCLK。
- PB8 为 SCL，PB9 为 SDA，使用 I2C1 AF4 开漏。V1 BSP 检查 I2C123 的时钟选择为 D2PCLK1、频率为 120 MHz。独立工程链接 I2C HAL 和滤波支持。
- RMII、DHCP、DCMI、FMC、JPEG FIFO 和 E7 私有帧所有权路径沿用 E7。DMA 及缓存区沿用 MPU Region 4/5，私有 JPEG 仍位于 `0xD07F0000`。

## 构建和验证

```powershell
python -B Tests/Ethernet/build_firmware.py --e7-v1
```

独立工程位于 `MDK-ARM/h7_mac_ethernet_e7_v1.uvprojx`，AXF 和 MAP 位于 `output/Ethernet/e7_v1/`。原 `--e7` 仍选择 V0 的独立工程和输出目录。

| 项目 | 结果 | 证据 |
| --- | --- | --- |
| E7 V1 完整构建 | PASS | 0 Error / 0 Warning。Code 111510 B，RO 4154 B，RW 500 B，ZI 128060 B |
| V1 BSP ARM 仿真 | PASS | 使用原提交中的 24 个测试用例，测试驱动直接包含本轮使用的 V1 BSP。覆盖供电顺序、I2C1 初始化、寄存器读写、NACK / BUSY / TIMEOUT 及定时寄存器预算 |
| MAP 审计 | PASS | 命中 I2C1 HAL 初始化、发送及接收函数。DMA 区 `0x24078000`，Region 5 剩余 18832 B，60 KiB JPEG 为 `UNINIT` / `Zero RW` |
| V1 实板 | NOT_RUN | 等待下载验证、相机芯片 ID、画面与后续 E7 验收 |

AXF SHA-256 为 `8e7af4bfe76dc64a08c222d192dffec4fca322d2d4d687aa24c0e3a80f3ede56`。命令及结果保存在本地忽略目录 `output/Ethernet/e7_v1_arm_test/`，构建记录为 `output/Ethernet/e7_v1_result.json`。

## 上板顺序

用 V1 工程人工下载 `eth_e7_v1.axf`。在 Keil 使用现有 E7 Watch 变量，先核对 `g_e7_sdram_result=0`、`g_e7_sensor_id=0x5640`、`g_e7_camera_init=0`、`g_e7_camera.running=1`。如相机仍无法初始化，先检查 PA15/U10 TP2、PH13/PH15、PA8 XCLK 和 PB8/PB9 地址 ACK，并定位首次失败的调用。冷上电和复位阶段的实际电压时序仍需在 V1 实板上读取。

WLAN 共享到 PC 的板卡直连网卡时保留 `g_e7_mode=1`。等 `g_e7_address.lease_valid=1` 后，按 `g_e7_address.ip.addr` 打开板卡网页，移动镜头前物体确认画面更新。后续见 [E7 实板验收](E7_HARDWARE_VALIDATION.md)。

## V1 MJPEG 数据完整性对照

在 V1 板的 SDRAM 版本上，HTTP 首页和 multipart 路由均返回 200。PC 用二进制 HTTP 读取 32 个完整分片，逐帧比较板端 `X-Frame-CRC32` 并用 Pillow 解码。11 帧 CRC 正确且解析为 160×120 RGB，21 帧 CRC 不符且 JPEG 头部无法解码。后续独立采样取得 18 个完整分片，其中 TCP 流内 JPEG 起点偏移模 4 为 0 或 3 的 11 帧均通过，模 4 为 1 或 2 的 7 帧均失败。错误有重复的字节模式，替换前 32 个字节仍不能恢复整帧 CRC。该结果支持排查 SDRAM 私有副本到 TCP 队列的复制路径，具体产生错误的指令或硬件环节尚未确定。此前单独观察到的采集停机仍为 OPEN。

```powershell
python -B Tests/Ethernet/build_firmware.py --e7-v1-axi
```

对照版仅移动 CPU 独占的 60 KiB JPEG 副本到 `0x24020000..0x2402F000`。Region 6 覆盖 `0x24020000..0x24030000`，配置为缓存、XN、非共享，DCache 保持开启。Region 2 的相机硬件 FIFO、Region 4 的 Ethernet DMA、Region 5 的 TCP 环及 lwIP 内存均沿用原地址和所有权。原 V1 SDRAM 工程和固件由 `--e7-v1` 保留。

| 项目 | 结果 |
| --- | --- |
| AXI 对照版完整编译 | PASS，0 Error / 0 Warning |
| MAP 检查 | PASS，CPU 私有 JPEG 为 61440 B、Zero RW、UNINIT。Ethernet DMA 及 Region 5 通过原检查，Region 5 剩余 18832 B |
| AXI 对照版实板 | NOT_RUN，等待首次出图、CRC 和稳定性对照 |

AXF SHA-256 为 `a26f81a2cfa0618e4579b0f828aeeb23befd9e5cc2b121461d8a7917b2ffdd75`。手动使用 `MDK-ARM/h7_mac_ethernet_e7_v1_axi.uvprojx` 下载 `output/Ethernet/e7_v1_axi/eth_e7_v1_axi.axf`。等 DHCP 获租后读取新 IP，仅打开一个 MJPEG 客户端。先确认网页画面随物体移动；关闭网页，再用既有工具执行 30 秒 CRC 对照，将命令中的 IP 换成当前板卡地址。

```powershell
python -B Tests/Ethernet/e6_peer.py --camera --stream-only --board-ip 192.168.137.190 --seconds 30 --output output/Ethernet/e7_v1_axi_board/stream.json
```

## V1 320×240 JPEG 与帧率验证

独立目标 `--e7-v1-qvga` 让 OV5640 和 CameraCapture 使用原有 320×240 JPEG 模式。帧提供器使用同一个尺寸常量并拒绝尺寸不匹配的帧。继续使用 CPU 私有 AXI SRAM 副本，网络和相机 DMA 地址均沿用 V1 AXI 对照版。原 160×120 的两个 V1 固件继续可用。

本目标默认 `g_e7_period_ms=50`，名义发送上限 20 fps。原 E7 的 200 ms 默认值对应 5 fps。E6 E7 共用的 HTTP 初始化接口目前接受 50–5000 ms，该范围来自软件参数门禁。50 ms 对应 20 fps 理论上限，25 fps 需要 40 ms 且需要重新评估软件门禁和 ACK 调度。当前发送状态机等待整帧数据的 TCP 确认，再调度下一帧。以上频率均为配置口径。

```powershell
python -B Tests/Ethernet/build_firmware.py --e7-v1-qvga
python -B Tests/Ethernet/run_e6_host.py --dynamic --qvga
```

Keil 工程为 `MDK-ARM/h7_mac_ethernet_e7_v1_qvga.uvprojx`，AXF 为 `output/Ethernet/e7_v1_qvga/eth_e7_v1_qvga.axf`。本轮完整编译 0 错误、0 警告，MAP 核对 DMA 区、I2C1 和 AXI JPEG 区均通过。Host 动态 MJPEG 回归和 QVGA 帧尺寸校验通过。AXF SHA-256 为 `31338cdfd8f4edfc621c5abc53afe829f2a5c5a117b0b3b50ed21a6432db5b0f`。读取 AXF 初始化数据确认 `g_e7_period_ms=50`。原 66 ms 固件与构建证据归档在 `output/Ethernet/e7_v1_qvga_15fps/`，原 AXF SHA-256 为 `0398b58d64d0a77517fc0ce668ee6e6074a65ae19bb71d489cd4d1d10ec3b960`。

实板项目为 NOT_RUN。人工下载后等 DHCP 获租，打开新 IP。以 PC 解码尺寸确认 320×240，逐帧校验 CRC32，并观察 `g_e7_camera.capture_fps_milli`、`g_e7_submission_fps_milli` 和 `g_e7_http.stream.current_fps_milli`。三个数值分别是采集、提交和 TCP 确认的帧率乘以 1000。需要对比其他帧间隔时，在相机初始化前停在读取地址模式的位置，修改 `g_e7_period_ms` 后继续运行，避免在启动采集后暂停 CPU。
