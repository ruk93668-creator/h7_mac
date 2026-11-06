# Stage03A HAL/JPEG接收审计

基线c4bd0ad；分支codex/camera-jpeg-stage03a。审计先于BSP实现。

## 实际源码

- HAL_DCMI_Start_DMA：Length<=65535时HAL_DMA_Start_IT，注册DCMI_DMAXferCplt，最后置CAPTURE。
- DCMI_DMAXferCplt：DMA数量完成后才使能FRAME；短JPEG不会填满接收窗口，不能等此链路交付FRAME。
- HAL_DCMI_IRQHandler：FRAME回调前关闭Snapshot的ERR/OVR并清FRAMERI；BSP仍先检查未屏蔽原始错误，回调只锁存状态、不扫描像素。
- HAL_DCMI_Stop：关闭采集/DCMI，调用HAL_DMA_Abort却忽略其返回值；因此不能用Stop返回值单独证明内存写入完成。
- HAL_DMA_Abort的注释说明：EN清零要等当前单次传输结束；HAL的DMA_Init允许独立配置直传、32位等宽和SINGLE突发。

## 采用方案

JPEG独立启动，显式开启FRAME，不依赖DMA TC开FRAME。仅JPEG采用NORMAL、32位等宽、直传、SINGLE访问，消除DMA FIFO剩余不足阈值无法落入内存的不确定性；原YUY2 FIFO/INC4配置保存并在Stop恢复。

任务中依次检查FRAME已观察、CAPTURE已自动清零、DCMI_SR.FNE为0，随后请求DMA EN清零，再次等待EN确实为0。确认DCMI FIFO仍空、无错误且0<NDTR<接收Word容量，才计算received_bytes=(capacity_words-NDTR)*4。直传没有DMA FIFO；EN停止确认保证在途单次内存写结束。DSB用于CPU访问排序，不把它说成能替DMA完成总线写。

容量耗尽（NDTR=0/TC）保守判为NO_RESOURCE，包括恰好填满窗口，不交付可能截断的数据。单次最多262140B，不实现分段。结束后先执行原Stop/HAL恢复，成功才向Media交付长度。FRAME、NDTR或HAL READY任何一个单独都不足以证明成功。FNE或EN未收尾时返回BUSY，由Media有界超时与Stop处理，不增加猜测性延时。

Media在任务中解析JPEG标记段与扫描数据，要求SOI、合法段边界、SOF分辨率、SOS及EOI；按JPEG结构找到首个EOI后，将当前位置作为有效文件长度；不把接收尾部加入文件，也不将未知尾部假定为正常填充。容量耗尽和硬件错误仍优先拒绝。此检查不是JPEG熵解码器，最终还必须用PC解码器验证真实文件。

## 资料和限制

ST AN5020的Snapshot、JPEG、数据存储小节说明硬件同步JPEG、尾Word零填充和CAPTURE自动结束：
https://www.st.com/content/st_com/en/technical-documents/an5020-introduction-to-digital-camera-interface-dcmi-for-stm32-mcus-stmicroelectronics_asset/chunk-005-topic-an5020-capture-modes.html

寄存器定义使用当前stm32h747xx.h；HAL停止语义依据当前stm32h7xx_hal_dma.c。RM0399在线PDF超过浏览读取大小，本机下载返回HTTP567，未声称完整读过该PDF；保留参考链接：https://www.st.com/resource/en/reference_manual/dm00176879-stm32h745755-and-stm32h747757-advanced-armbased-32bit-mcus-stmicroelectronics.pdf

直传模式在本板JPEG吞吐及FRAME/FNE/EN实际时序需要实板验证。若出现DME/OVR，必须报告而不是关掉错误检查。没有真实JPEG文件前不宣布硬件验收完成。

## 实板数据补充

用户导出的1104B SDRAM数据包含一幅1085B、160×120的可解码JPEG，原始数据及SHA见Hardware目录。旧尾部零填充限制造成误报，已以真实数据加入Media测试32。导出长度与received是否同轮尚未确认；修正后固件交付和停止恢复仍待实板验证。
