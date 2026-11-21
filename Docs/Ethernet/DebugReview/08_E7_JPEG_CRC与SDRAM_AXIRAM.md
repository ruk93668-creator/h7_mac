# E7 JPEG CRC、SDRAM 和 AXI SRAM

## 数据路径

相机 DMA 写 SDRAM 字节 FIFO，Media 交出一个完整 JPEG 借用。跨尾时用 `size_bytes-size_bytes2` 作为第一段，第二段从 FIFO 起点读取；数据归还以前两段均禁止 DMA 复用。测试 Provider 复制并计算 CRC 后归还原始帧，再以独立私有租约交付 HTTP。TCP 环同步复制最后的 JPEG 数据后归还私有租约，ACK 确认用于统计。Region 2 是相机 SDRAM；4 是 Ethernet DMA 非缓存窗；5 是 E7 CPU/lwIP；6 用于 AXI 对照的 CPU 私有缓冲 `0x24020000`，先前该副本位于 `0xD07F0000`。两版相机 FIFO 仍在 SDRAM。[E7_MEMORY_OWNERSHIP](../E7_MEMORY_OWNERSHIP.md)


## C12 V1 相机初始化与板级版本错配

### 1. 问题背景

E7 首次启用 OV5640 真实 JPEG，需验证电源、SCCB 与 Sensor ID。

### 2. 第一次异常

V1 上 g_e7_camera_init=0xFFFFFFF7。

### 3. 最初的怀疑

SDRAM、DCMI、SCCB 读回、传感器配置都可能返回 SYS_ERR_IO。

### 4. 排查过程

核对 E7 构建 SHA、bsp_camera.c 来源和 V1 历史 9bc95fb/8b87a7a；构建独立 V1 目标。

### 5. 关键突破

V0 的 PB8/PB9 功能顺序与 V1 相反，V1 还需要 PA15 DVDD enable 和 PH13 PWDN。

### 6. 根因及修复

**CONFIRMED**。已确认板级不匹配。独立 V1 适配使用硬件 I2C1，保留生产 BSP。

### 7. 验证结果

3f332fd，24 项 V1 BSP 仿真通过，随后板上读到 0x5640、初始化 0。[E7_V1_ADAPTATION](../E7_V1_ADAPTATION.md)

### 8. 底层原理

I2C 供电、复位和总线方向先于 DCMI；初始化成功后仍可能有运行期 DMA 故障。

### 9. 最终经验

遇到多级通用错误码，优先核板型和真正链接的 BSP。

### 10. 面试复盘

初次把 OV5640 测试镜像烧到 V1 后，初始化返回一个通用 IO 错误。我没有直接改 Camera 算法，而是把实际 AXF 的构建输入和仓库内 V1 迁移对照。定位到当时链接的是 V0 BSP，V1 除了 SDA/SCL 对调到硬件 I2C1，还有独立 DVDD 使能和 PWDN。采用已经存在的 V1 实现做独立目标，跑专项仿真后上板读出 0x5640，初始化返回零。这个案例展示从镜像溯源到硬件差异、再到最小改动复验的全过程。


## C13 SDRAM 私有副本版本的 CRC 破损

### 1. 问题背景

HTTP 已打开，确认浏览器反复显示 MJPEG 替代文本时的真实 JPEG。

### 2. 第一次异常

完整 32 个分片中 11 个 CRC 正确且解码，21 个头部损坏且 CRC 不符。独立 18 帧中起点模 4 为 0/3 的 11 帧通过，1/2 的 7 帧失败。

### 3. 最初的怀疑

在 CRC 计算之后的复制或传输环节可能有对齐相关错误，SDRAM 访问为线索。

### 4. 排查过程

用二进制 HTTP 与 Pillow 验证，排除 stdout 换行假象；比较字节偏移与坏帧，替换头 32 B 不能恢复 CRC。4c7d16d 只迁移 CPU 私有副本到 AXI。

### 5. 关键突破

损坏随偏移呈规律；后续 AXI、分段、快发组合有通过 CRC 的实板短测。

### 6. 根因及修复

**OPEN**。CRC 首次变坏的指令、硬件环节及单独 AXI 迁移的长稳结果未证明。

### 7. 验证结果

旧 32/18 帧统计来自历史报告；本轮未定位对应原始字节流文件。后续 55 帧 CRC 正常属于不同版本组合。

### 8. 底层原理

应用 CRC 是端到端核对。搬移 CPU 私有副本会改变地址及 MPU 命中，不能单独反推 SDRAM 器件故障。

### 9. 最终经验

在多个复制边界分别计算同一 CRC，再用同版本单变量对照决定第一处差异。

### 10. 面试复盘

网页首页返回 200，但画面会坏。我直接读取二进制 Multipart，并对每张 JPEG 比较长度、CRC 和解码结果。第一次 32 帧中有 21 帧 CRC 不符。下一轮按 TCP 流内 JPEG 起点模 4 分类，错误集中在特定偏移，替换前 32 字节仍不能恢复。我先只搬移 CPU 私有副本到 AXI，与 SDRAM 原帧 FIFO 分离。后续组合配置有短测通过，但这不足以指出第一次坏在哪个复制边界。因此我会把内存隔离实验与根因证明分开，并说明下一轮应在哪些边界重算 CRC。

源码（`a1fbe31`）`Tests/Ethernet/e7_camera_provider.c` 的 `E7Camera_Acquire()` 第 12 行、`E7Camera_CopySpan()` 第 131 行、`E7Camera_Copy()` 第 153 行；`CM7/Media/Src/camera_mjpeg_fifo.c` 提供字节 FIFO，`camera_capture.c` 管理借用。
