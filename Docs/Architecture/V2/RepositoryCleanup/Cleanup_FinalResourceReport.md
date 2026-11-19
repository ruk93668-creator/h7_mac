# Cleanup 1.4 资源报告

两组实际用户工程配置在隔离目录中重编，单位为字节。Flash 使用 Code+RO+RW。

| 构建 | Flash | RW | ZI | DTCM / CM4 RW_IRAM1 | AXI SRAM SDMMC DMA | Shared |
|---|---:|---:|---:|---:|---:|---:|
| b/CM7 | 187348 | 1828 | 107676 | 76416 | 32768 | 320 |
| c/CM7 | 186808 | 1828 | 106812 | 75552 | 32768 | 320 |
| b/CM4 | 28724 | 936 | 3576 | 4192 | 0 | 320 |
| c/CM4 | 28724 | 936 | 3576 | 4192 | 0 | 320 |

CM7 Flash 减少 540 B，ZI/DTCM 各减少 864 B。Legacy Camera 上下文由 988 B 降至 132 B。CM4 链接预算与 HEX 保持一致。外置 SDRAM 在两个 Map 中均无独立链接执行区域。Camera SDRAM 固定地址、窗口及缓冲定义来自未改动的 Media 源码，实板峰值未测。

静态 SDRAM 定义前后一致。BSP 窗口为 8 MiB（0xD0000000 起）；Camera stream pool 为 7 × 614,400 B（0xD0000400 起）；MJPEG FIFO 为 512 KiB 加两个 8 KiB 丢帧块（0xD0480000 起）。Snapshot 与单 JPEG 的容量约束也来自同一份未改动的 CameraCapture 源码。

| CM7 静态栈对象 | 清理前 / 清理后 |
|---|---:|
| platform_rtos.o/s_idle_stack | 1024 / 1024 |
| camera_task.o/s_camera_stack | 6144 / 6144 |
| usb_task.o/s_usb_stack | 8192 / 8192 |
| control_task.o/s_control_stack | 3072 / 3072 |
| ipc_task.o/s_ipc_stack | 2048 / 2048 |
| storage_task.o/s_stack | 8192 / 8192 |

任务优先级和栈配置源码在两个提交之间无变化。Map 数据是静态分配，运行期栈高水位仍为 NOT_RUN。
