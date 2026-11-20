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
