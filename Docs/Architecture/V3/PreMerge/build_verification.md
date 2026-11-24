# 构建与资源核验

ARMCC5 V5.06 update 7，经源码候选的独立输出验证。四套完整 Rebuild 均为 0 Error、0 Warning。

| 配置 | Code B | RO B | RW B | ZI B |
| --- | ---: | ---: | ---: | ---: |
| R1 产品 JPEG | 139606 | 3994 | 1296 | 100992 |
| 合并候选产品 JPEG | 140338 | 3922 | 1296 | 100960 |
| 合并候选产品 Raw | 140338 | 3922 | 1296 | 100960 |
| R1 联合测试 | 142654 | 4246 | 1308 | 101620 |
| 合并候选独立联合测试 | 142734 | 4246 | 1308 | 101620 |
| CM4 候选与 R1 | 26302 | 1438 | 936 | 3576 |

同配置产品 Flash 的 Code+RO 增加 660 B，RW 持平，ZI 减少 32 B。正式镜像保留 Supervisor 入口和诊断，并链接 Raw/JPEG 两条真实 Submit 路径。CM4 HEX 逐字节匹配冻结版，SHA256 为 `3030484c03d4983a192be39142dce7b6dfdb8e6c93a49e1f8285ee846540fb17`。

### 正式配置

- 默认是 OV5640 JPEG、320×240、目标 25 FPS，UVC 帧上限 40,000 B。Raw 正式构建选择 YUY2、160×120、目标 10 FPS，每帧 38,400 B。
- SD 默认 MBR 分区 3，独立板测分区 1。AVI 单段最多 256 MiB，包含头部、帧块和 `idx1`。无默认时长截止，索引容量 65,536 帧。下一帧超限前结束准入，归还后完成 Finish、Close、Unmount。保留原有的创建新文件规则。
- 配置结构中的累计额度、预留字节和段数仍是历史保留字段，当前没有滚动覆盖或自动分段策略。

### 地址和任务

| 项目 | 范围或配置 |
| --- | --- |
| CM7 DTCM | `0x20000000`，128 KiB 链接窗口，候选 Map 使用 `0x10E30` B |
| AXI SRAM SDMMC/IDMA | `0x24000000`，32 KiB 专用中转 |
| 双核共享 | `0x38000000`，4 KiB 窗口 |
| Raw SDRAM | `0xD0000400` 起，7 块，每块 614,400 B，两个活动 DMA 目标 |
| JPEG SDRAM | `0xD0480000` 起 512 KiB FIFO，尾随两个 8 KiB 丢弃缓冲，七个 Loan 记录 |
| AVI 索引 | `0xD0590000` 起 1 MiB |
| Camera / USB / Storage | 1536 / 2048 / 2048 words，优先级 3 / 4 / 1 |
| Control / IPC | 768 / 512 words，优先级 2 / 1 |

现有 8 MiB 非缓存 SDRAM MPU 窗口、IRQ、链接地址和任务栈未改。两个 CM7 产品 Map 均保留 Media Lease、Reader Proof、AVI、Supervisor 和诊断，未链接模拟分发表、板测邮箱、BoardSmoke、旧 Application 入口或重复任务创建函数。编译日志和散列表见 `Keil/verification.json`、`KeilTest/verification.json`、`map-audit.json`。
