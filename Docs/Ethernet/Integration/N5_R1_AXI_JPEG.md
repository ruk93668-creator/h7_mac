# N5-R1 — JPEG 私有缓冲迁移至 AXI SRAM

**READY_FOR_REVIEW**。软件构建、MAP/MPU 审计和主机模拟同帧 CRC 检查均通过。N5 实板验收和 Region 6 硬件读回均为 **NOT_RUN**。等待独立审核，R2 从审核通过的 R1 提交另行开始。

- 审核日期：2026-11-28（Asia/Shanghai）。
- 冻结基线：`78580073610ff4cfe06a4b004b1b0d75bffef499`。
- 经验证的实现提交：`322f8b780f7b3717ca2c34b93b84f12341488818`。后续交付提交仅增加本报告及证据。

## 内存布局和 MPU

| 用途 | 冻结基线 | R1 |
|---|---|---|
| Camera JPEG FIFO | 原 SDRAM，Region 2，非缓存 | 同一地址及属性 |
| NetworkService 的 `s_jpeg` | `0xD07F0000–0xD07FEFFF`，Region 2 | `0x24020000–0x2402EFFF`，Region 6 |
| JPEG Scatter 保留容量 | `0xF000`，UNINIT | `0xF000`（61440 字节），UNINIT |
| MPU JPEG 窗口 | SDRAM 的 8 MiB 窗口覆盖私有副本 | `0x24020000–0x2402FFFF`，64 KiB。末尾 4 KiB 保留，未分配 |
| SDMMC DMA / lwIP / Ethernet DMA | `0x24000000–0x24007FFF` / `0x24010000–0x2401FFFF` / `0x24078000–0x2407FFFF` | 相同区域基址、使用量、容量 |
| IPC 与 CM7 DTCM | `0x38000000` 与 `0x20000000` | 相同区域基址、使用量、容量 |

冻结正式 CM7 的源码给 Region 0–5 分配了用途，Region 6 没有配置调用。正式 CM7 的 Flash 工程指定 `h7_mac_CM7_custom.sct`。`stm32h747xx_sram1_CM7.sct` 的备用 SRAM 调试链接布局会覆盖该 AXI 窗口。它未被这批正式目标选用。切换到该布局需重新审计。

R1 在原 `eth_memory_configure()` 的生产配置分支中添加 Region 6，并沿用 E7 AXI 对照版的 64 KiB 属性。`TEX=1 / C=1 / B=1 / S=0 / AP=3 / XN=1 / EN=1 / SRD=0`，预期 `RASR=0x130B001F`。MPU 配置在原启动路径中先于 DCache 开启，DCache 启用代码与 MPU 其余配置保持原样。运行正式 AXF 的配置函数并观察模拟 MPU 写入，Region 4/5 与基线相同，各 CM7 候选增加且仅增加 Region 6 的 `RBAR=0x24020000`、`RASR=0x130B001F`。这是候选可执行文件的写入证据，实际硬件读回仍待验。

所有正式 CM7 MAP 都证明 `.network_jpeg` 仅由 `network_frames.o` 提供，`s_jpeg` 唯一落点为 `0x24020000`，实际大小和上限都是 `0xF000`。整个 Region 6 窗口与其余正式执行区不交叉，CM4 MAP 无此段。网口和相机 DMA 使用各自原缓冲区。Camera FIFO、8 字节分段复制、DSB、CRC32、Frame Lease、TCP 发送状态机以及 lwIP 内存布局保持原样。

[MPU 写入证据](Evidence/N5R1/mpu-register-write-audit.json) · [MAP 对照和哈希](Evidence/N5R1/map-audit.json)

## 同帧数据完整性

生产 C 代码在 Cortex-M 主机模拟中运行。场景 128 将 E6 的同一张已编码 JPEG 放入模拟 Camera FIFO，最后 4 字节跨到 FIFO 开头。发送端使用生产 Frame Lease、JPEG 私有副本、TCP 应用环和 lwIP。测试从 `tcp_write()` 实际读取的应用环字节组成 multipart，并按 TCP 序号独立重组模拟网口提交。四段数据逐字节与原样本比较，帧头 CRC 也核对一致。

CRC-32/ISO-HDLC 使用反射多项式 `0xEDB88320`、初值和输出异或均为 `0xFFFFFFFF`。四段以同一 HTTP 会话中的 `X-Frame-Sequence: 1`、同一 `Content-Length: 5294` 配对，排除 HTTP 头与 multipart 分隔符。

| 采样点 | 字节数 | O0 CRC32 | O2 CRC32 |
|---|---:|---|---|
| Camera FIFO 原始 JPEG，合并两段 | 5294 | 0582F333 | 0582F333 |
| AXI 私有 JPEG 副本 | 5294 | 0582F333 | 0582F333 |
| TCP 应用发送环 | 5294 | 0582F333 | 0582F333 |
| 模拟 PC 对端按 TCP 序号重组 | 5294 | 0582F333 | 0582F333 |
| 真实 PC 通过板卡网口接收 | NOT_RUN | NOT_RUN | NOT_RUN |

[同帧对照原始结果](Evidence/N5R1/same-frame-crc.json)

## 构建与回归

从实现提交的干净副本，使用 ARMCC 5/Keil 完整重建以下目标。构建前后输入哈希一致，构建后无 tracked diff。

| 目标 | Error / Warning | 验证 |
|---|---|---|
| CM7 JPEG | 0 / 0 | MAP、MPU 写入通过 |
| CM7 Raw | 0 / 0 | MAP、MPU 写入通过 |
| CM4 | 0 / 0 | 原执行内存布局对照通过 |
| 正式 Network profile | 0 / 0 | MAP、MPU 写入通过 |
| N5 Hardware Acceptance profile | 0 / 0 | MAP、MPU 写入通过 |

ApplicationV3 **172/172** 通过，O0/O2 各 86 个。包含 Frame Lease 归还场景 109–115，真实 HTTP RST 场景 119，100 轮模拟收尾 120，N5 Producer 121–123、ReadyGate 127 以及同帧跨 FIFO 尾部场景 128。E5、E6、E7、E7 QVGA 的原有主机回归通过，包含 E7 跨尾复制与私有帧和归还检查。没有把模拟链路时长或 PC 对端结果计为板卡实测。

初始编译矩阵同时进行了主机测试编辑，全局源文件一致性为 false。初版 CRC 观察器中的字节转义错误也导致 O0/O2 的 TCP 环与线上采样未匹配。保留两项初始结果，修正观察器后从冻结源码重跑。正式结论采用冻结提交的最终结果。历史板端 CRC 失败和停服的原始抓包仍在此前的 [N5 网口抓包归档](https://github.com/ruk93668-creator/h7_mac/tree/38e1e478640c5b2de9f17cebd162ef7d44ee4b83/Docs/Ethernet/Integration/Evidence/N5Hardware_2026-11-26/Captures)，R1 不关闭这些实板问题。

[完整 Keil 与主机构建日志及结果](Evidence/N5R1/) · [构建矩阵汇总](Evidence/N5R1/build-results.json)

## 修改文件

- M `CM7/ApplicationV3/Services/Network/network_frames.c`
- M `MDK-ARM/h7_mac_CM7_custom.sct`
- M `Platform/Ethernet/eth_memory.c`
- M `Tests/ApplicationV3/hardware_fixture.c`
- A `Tests/ApplicationV3/jpeg_crc_trace.py`
- M `Tests/ApplicationV3/run_tests.py`
- M `Tests/ApplicationV3/startup_test.c`
- M `Tests/CameraLifecycleC1/run_tests.py`
- M `Tests/Ethernet/build_production.py`
- M `Tests/Ethernet/run_production_matrix.py`

## 复核命令

```powershell
# 在独立工作目录执行
python -B Tests/Ethernet/run_production_matrix.py --section build --formal-only --output output/r1/build
# 配好测试依赖 unicorn 和 pyelftools 的 PYTHONPATH 后执行
python -B Tests/ApplicationV3/run_tests.py output/r1/appv3 --production
# 以下在 MSVC x64 开发者命令环境中执行
python -B Tests/Ethernet/run_e5_host.py
python -B Tests/Ethernet/run_e6_host.py
python -B Tests/Ethernet/run_e6_host.py --dynamic
python -B Tests/Ethernet/run_e6_host.py --dynamic --qvga
```
