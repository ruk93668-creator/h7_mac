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

## V1 20 fps DMA FIFO 故障对照

板上记录。在 320×240 / 50 ms 配置下，关闭网页空跑 60 秒期间采集帧数持续增长，断点未触发。保持同一轮运行，打开网页后立即命中 JPEG 连续采集的保护性停机前断点。Locals 记录 `events.flags=0x02`、`result=-9`、`dcmi_error=0`、`dma_error=0x02`，复现 DMA1 Stream0 的 `HAL_DMA_ERROR_FE`。打开页面同时触发 CPU 读取 SDRAM、JPEG 校验复制及 TCP 发送，现有结果不能再细分触发条件。

加入 E7 独立诊断开关。人工下载新版后停在 `e7_main.c` 读取地址模式的行，设置 `g_e7_copy_only=1`，保持 `g_e7_period_ms=50`，继续运行。该模式保留 Ethernet 基础轮询和 DHCP，每个周期走与 MJPEG 相同的取帧、读 SDRAM、复制到私有 AXI 缓冲、计算 CRC 和归还流程。采样结束立即释放私有租约；HTTP/TCP 应用未启动。预期 `g_e7_copy_attempts` 和 `g_e7_camera.copies` 持续增长，`g_e7_transport.listening=0`。保持网页关闭，观察 60 秒并保留首次错误的 Locals。默认 `g_e7_copy_only=0`，运行原网页路径。

如果只复制也再现 `dma_error=0x02`，本轮能证明 HTTP/TCP 发送不是触发的必要条件。如果未复现，只能说明该次复制对照窗口稳定，需要按相同帧数和时长复比。实板复制对照为 NOT_RUN，生产 Camera 和 Ethernet 驱动保持原样。

编译 0 错误、0 警告，既有动态 MJPEG 与相机取帧归还 Host 回归通过。新版 AXF SHA-256 为 `73d52c6f6b27ead8e2353c0b44cd83d51bfb5937553b50bee8163098b91a87ee`，原 20 fps 板测固件和 MAP 归档于 `output/Ethernet/e7_v1_qvga_before_copy_probe/`。

### 拔线复验结果

| 320×240 对照条件 | 实板观察 |
| --- | --- |
| 网页关闭、未主动复制 | 连续采集 60 秒，采集计数增长，未命中错误断点 |
| 打开网页、50 ms | 打开后命中，`dma_error=0x02`、`dcmi_error=0` |
| 只复制、50 ms | 复制尝试 28 次，Watch 显示已复制 24 帧，随后命中同一错误 |
| 只复制、200 ms，网线连接 | DHCP 和静态地址轮次均命中同一错误 |
| 只复制、200 ms、静态模式、网线拔出 | 命中同一错误，`events.flags=0x02`、`result=-9`、`dma_error=0x02`、`dcmi_error=0` |

已在没有有线链路和 DHCP 交互的条件下复现。排查转向相机 DMA 写 SDRAM 与 CPU 读取并复制原始帧的并发路径。当前连续 JPEG 配置使用 FIFO FULL、内存 INC4、外设 SINGLE。已有 [DMA FIFO 排查记录](../Debug/DCMI_DMA_FIFO错误排查记录.md) 描述过相似负载触发条件，但当前故障的实际触发环节仍为 OPEN。下一轮在 `Dcmi_RecordError()` 首次命中时记录调用栈、`s_dcmi_dma.ErrorCode` 及 Stream0 的 `CR`、`FCR`、`NDTR`。保留生产驱动设置和错误检测。

### 分段读取诊断

底层断点记录显示 DMA1 Stream0 中断进入 `Dcmi_CheckErrors()` 时，`DMA1_LISR=0x11` 同时包含 FE 与 HT 标志。异常打断了地址 `0x08000898` 处的 `__rt_memcpy_w`，该处为四字加载指令。CPU 停止后读回 `CR=0x008E5517`、`FCR=0xA7`、`NDTR=0x060C`，确认配置为 FIFO FULL、内存 INC4、外设 SINGLE，并启用双缓冲。读回时 FIFO 为空，读回值和中断锁存值属于两个时点。

新增独立 E7 对照开关 `g_e7_copy_segmented`。与 `g_e7_copy_only=1` 一起在初始化前设为 1 后，每次 CPU 从 SDRAM 最多复制 8 字节，每段后执行 DSB，再读取下一段。沿用原 JPEG 缓冲租约、CRC、相机 DMA 参数和复制周期。保持静态模式、网线拔出、200 ms、网页关闭。先测分段 60 秒，再在同一固件上将开关设为 0，使用相同条件对照整段复制。记录首次错误与成功复制帧数；实板对照为 NOT_RUN。

编译 0 错误、0 警告。Host 跨尾分段次数、逐字节一致性、CRC 与归还回归通过。新版 AXF SHA-256 为 `f67ca10df92a77d94b55685ee57736b3bfd5228aa39bca377c7b4b168aea52b2`，原只复制对照固件和 MAP 归档于 `output/Ethernet/e7_v1_qvga_before_segmented_probe/`。

### 分段与整段对照及网页验证准备

实板反馈中，8 字节分段读取轮未命中断点，尚缺时长和复制计数。恢复整段模式，保持只复制、静态 IP、200 ms 和网线拔出后，在第 5 次复制尝试时命中 `Dcmi_RecordError()`，Watch 快照记录已复制 3 帧，`CR=0x00865517`、`FCR=0xA7`、`NDTR=0x0284`。该截图未显示本次错误位，历史多轮已经读到 DMA FIFO 错误 `0x02`。

E7 新版允许网页模式使用同一个分段开关。下载后在读取地址模式前设置 `g_e7_mode=1`、`g_e7_copy_only=0`、`g_e7_copy_segmented=1`、`g_e7_period_ms=200`，连接网线后按 F5，待获租打开一个网页观察 60 秒。记录首帧、帧完成计数和错误断点。若通过，再从相同设置开始只把帧间隔改为 50 ms，验证 20 fps 调度档。网页分段实板结果为 NOT_RUN。

编译 0 错误、0 警告，既有 Host 回归和 MAP 检查通过。新版 AXF SHA-256 为 `16056c4747a0a0e0f56d0f8b3027c3e4b475c0a21c4b76da26d223a62897ad5c`。上一版诊断 AXF 和 MAP 保存在 `output/Ethernet/e7_v1_qvga_before_segmented_http/`。
