# E7 相机 DMA FIFO Error 完整排查

## C14 CPU 复制期的 DMA1 Stream0 FE

### 1. 问题背景

320×240 QVGA 与 50 ms 流调度启用后，验证连续采集和网页输出。

### 2. 第一次异常

先读到 media_result=-9、capture_errors=1、release_errors=0；在保护性停机之前观察 events.flags=2、dcmi_error=0、dma_error=2。

### 3. 最初的怀疑

先考虑相机采集、原帧归还、网页/TCP、DHCP、网络 DMA 和 SDRAM 读写并发。取得 IP 的时刻曾看起来有关。

### 4. 排查过程

逐次撤掉变量：不开网页时采集满 60 s、帧数增长；打开网页立刻命中。88f9feb 引入只复制：50 ms 时 28 次尝试、快照已复制 24 帧后报 FE。200 ms 仍报，改静态 IP、拔网线仍报（2a2e0de）。底层断点显示 IRQ 打断 memcpy。320640d 加 8 B + DSB；分段轮用户称无命中，缺精确时长与帧数。整段反向对照第 5 次尝试命中，同图未显示错误位，旧多轮已经确认 FE。c899784 才把对照开放给网页。

### 5. 关键突破

调用栈为 DMA1_Stream0_IRQHandler → Dcmi_CheckErrors → Dcmi_RecordError。LISR=0x11 包含 FE 与 HT。IRQ 打断 __rt_memcpy_w 的 0x08000898，历史反汇编为 LDM 四字读取。这个堆栈证明中断发生于 CPU 复制时段。

### 6. 根因及修复

**SUPPORTED**。已确认 IRQ 捕获的 FE 和断网复现。现象支持 CPU 读取相机共享 SDRAM 时的并发路径，唯一触发机制 OPEN。分段读取是缓解候选，保留原错误检测和 DMA 设置。

### 7. 验证结果

用户观察 200 ms 网页稳定，具体长测时长未归档；快速发送短采样 9.33 fps、55 帧 CRC 匹配。未建立该组合的正式长稳。[E7_V1_ADAPTATION](../E7_V1_ADAPTATION.md) · [更早的 USB UVC 对照](../../Debug/DCMI_DMA_FIFO错误排查记录.md)

### 8. 底层原理

相机 DMA 在 D2/总线矩阵/FMC 路径向 SDRAM 写，CPU 读取原帧也经过共享路径。CPU 停在断点后 DMA 寄存器可能继续变化；FE 可与 HT 同时置位，HT 与稍后 NDTR 不是同一采样点。DSB 约束 CPU 前序访问完成，不能证明 SDRAM 硬件有缺陷或替 DMA 停机。

### 9. 最终经验

先抓首次 IRQ 和 LISR，拍实际 CR/FCR，保留真正出错版本与单变量数据。后续可量化访问方式、FMC 时序与失败分布。

### 10. 面试复盘

打开网页后，相机报 -9。我用出错前断点读到 DMA FIFO error，然后逐步关掉网页、TCP、DHCP，并拔掉网线，只复制仍然复现。把断点前移到底层，看到 DMA IRQ 打断 memcpy，LISR 已有 FE；稍后 CR/FCR 确认实际使用 INC4 和 FULL，排除了早期 UVC 发生过的配置覆盖。随后比较整段复制和八字节加 DSB，分段版有未命中的观察，反向又触发错误。证据缩到共享 SDRAM 的并发窗口，但长稳和唯一硬件机制尚未关闭。


## 寄存器与所有权现场

| 观测 | 原始值 | 解读和边界 |
| --- | --- | --- |
| IRQ 读取 `DMA1_LISR` | 0x11 | 0x01 FE、0x10 HT。HAL 层映射 `HAL_DMA_ERROR_FE=0x02`，两套编码分别使用 |
| `events.dcmi_error` | 0 | 当次记录无同步或 DCMI 溢出位 |
| 暂停读 `CR` | 0x008E5517 | EN、DBM、CT=M1，32 位，内存 INC4、外设 SINGLE |
| 暂停读 `FCR` | 0xA7 | FEIE、FIFO 模式、FULL 阈值；读回时 FS=4 为空 |
| 暂停读 `NDTR` | 0x060C | 1548 个 32 位字剩余；当时的读值约 6 KiB |
| 反向对照暂停 | CR=0x00865517、FCR=0xA7、NDTR=0x0284 | 当前目标变为 M0；比较的是不同停止快照 |

源码（`a1fbe31`，`git blame` 该检查函数可追到分支基线之前） `CM7/BSP/bsp_dcmi.c` 第 1239 行 IRQ 先检查再进 HAL，第 1362 行记录错误，第 1388 行读取原始标志，第 1034 行附近设置连续模式 DMA。Media 在 `CM7/Media/Src/camera_capture.c` 第 552–566 行取事件并进入保护性 Stop。JPEG 提供器的原帧租约继续有效到归还，失败进入隔离。UVC 历史也曾在 CPU 读 SDRAM 时复现 FE，并有 INC4 被 SINGLE 覆盖的旧问题；E7 的实际 CR 排除了这项配置覆盖，历史 UVC 试验不代替 E7 的实测。
