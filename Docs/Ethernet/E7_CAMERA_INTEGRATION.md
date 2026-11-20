# E7 Camera 与 Ethernet 资源审计

复用 CM7 的 bsp_camera、bsp_dcmi、bsp_sdram、OV5640 Device/组件、Device Registry、camera_capture 和 camera_mjpeg_fifo。E7 的设备链接区只含 OV5640 导出项；入口明确调用 BSP 与单设备初始化，不执行 Framework Initcall、CameraTask、USB、Storage、IPC 或 Application V2 初始化。相机侧不注册上层帧回调，生产 DCMI ISR 继续记录 DMA 状态及推进块指针，JPEG 解析、复制、归还和 lwIP 都在单循环执行。

| 资源 | 联合核对 |
| --- | --- |
| PLL1 / XCLK | HSI 64 MHz / 4 × 60，Q 除 4 得 240 MHz，PA8 MCO1 除 10 得 24 MHz。BSP 初始化核对 PLL ready、Q enable 和频率。 |
| PLL2 / FMC | BSP 使用 PLL2R 220 MHz，SDCLK 再除 2。PLL1 与 PHY 的外部 50 MHz RMII REF_CLK 保持原配置。 |
| RMII 和复位 | PA1/2/7、PC1/4/5、PG11/13/14、PG9。 |
| DCMI 和相机控制 | DCMI 用 PC6/7、PG10、PH12、PE4/5/6、PB6/7、PA4/6；控制用 PA8、PB8/9、PH15。 |
| FMC | 使用 PD0/1/8/9/10/14/15，PE0/1/7–15，PF0–5/11–15，PG0/1/2/4/5/8/15，PH5/6/7。以上三组互不重叠。 |
| IRQ | DCMI 和 DMA1 Stream0 为两个向量，优先级均为 5，ETH 单独向量，SysTick 由独立固件维护 HAL tick。BSP 停止只关相机两路 IRQ，硬件停机等待留在主循环，SysTick 可继续运行。 |

DMA1 能经总线矩阵访问 FMC SDRAM；Ethernet 描述符和 DMA 数据仍在已用的 AXI SRAM 窗口。并行总线带宽和错误率需在实板测量。[ST RM0399 第 2 章及表 2](https://www.st.com/resource/en/reference_manual/rm0399-stm32h745755-and-stm32h747757-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)

安全 Stop 使用既有 CameraCapture_Stop。BSP 先等待 DMA Stream EN 清除、DCMI CAPTURE/ENABLE 清除并恢复 HAL，超时返回失败。适配层同时检查 CameraCapture_IsStopped 与 StopFacts，再停传感器。捕获失败只记录，不自动重启。原帧归还失败时锁住原借用，拒绝重新进入采集或人工 Stop 路径；当前生产 API 对活动采集且有借用的正常 Stop 本身也返回 BUSY。故障时 FIFO 继续服从生产块引用约束，不强清硬件或计数。实板必须复核停止时的寄存器读回和故障隔离。
