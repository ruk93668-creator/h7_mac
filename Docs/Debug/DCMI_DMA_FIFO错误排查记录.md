# DCMI → SDRAM → UVC：DMA FIFO错误排查记录

记录日期：2026-11-02。正式工程：D:/32/STM32H7/h7_mac。

**当前结论：在用户报告的对照测试中，单独采集和只持有图像均稳定，加入CPU从SDRAM读取并复制到内部RAM后即可触发DMA FIFO错误。实际USB视频发送不是该测试中触发错误的必要条件。具体硬件根因、INCR4配置后的长期稳定性仍待验证。**

## 1. 链路与故障现象

```text
OV5640 → DCMI → DMA1_Stream0双缓冲 → SDRAM三缓冲
                                      ↓
                           Media交付READY → IN_USE
                                      ↓
                         App_UvcBridgeProcess
                                      ↓
                        UVC分包读取图像 → EP1 IN
```

现象：摄像头单独连续采集稳定，电脑打开视频后出现黑屏或不再推进。曾怀疑Media只交付前两帧、USB不再允许提交；这些起初是待验证假设，并非已经观测到主机收到两张图像。

桥接Watch截图实际显示过：state=IDLE、submitted_frames=1、released_frames=1、rejected_frames=0、release_pending=false。这证明该次桥接提交过一次并归还了Media图像，不能证明USB完整发送了一帧。

|桥接字段|含义|
|---|---|
|state=0 / IDLE|当前未持有Media图像|
|state=1 / HELD|持有Media图像，USB尚未借用或已归还|
|state=2 / LOANED|USB仍可能访问原图，禁止归还Media|
|submitted_frames|Submit接受借用次数，不是主机收到的图像数量|
|released_frames|Media成功接收归还次数，包含丢弃图像后的归还|
|rejected_frames|图像校验失败或Submit失败的次数|
|release_pending|归还失败，后续只重试归还，不再重复Submit|

## 2. USB抓包证据

文件：D:/32/STM32H7/STM32H747_UVC_BlackScreen_3.pcapng。

- 设备地址5，Probe GET/SET和Commit成功。
- 协商参数：38400B/帧，帧间隔1000000个100ns单位，即10FPS，最大Payload为512B。
- 第1012～1013包：VS Interface 1成功切到Alt1。
- EP1 IN有132次完成的等时URB，共4224个等时子包；全部返回长度0，总Payload为0B。
- 约6.06秒时主机执行Abort/Reset Pipe并切回Alt0；仅凭这个结束动作不能认定是首次故障原因。
- USBPcap的SUCCESS状态不等于返回了视频数据；这是主机URB层记录，不是硬件总线分析仪的逐事务证明。

对比历史STM32H747_UVC_BlackScreen_ok.pcapng：107次EP1等时完成，其中105次有数据，共1300888B（包含UVC Payload头等传输数据，不等于纯图像像素字节数）。因此本次0B不是把所有接收请求都误当作无数据导致的统计结果。

## 3. 找到首次采集错误

`s_capture.last_result = 0xFFFFFFF7`，按int32_t解释为-9，即SYS_ERR_IO。随后在首次Dcmi_RecordError断点取得：

```text
result                 = -9
s_dcmi.ErrorCode        = 0
s_dcmi_dma.ErrorCode    = 2
```

当前HAL错误位：TE=1，FE=2，DME=4。**2是HAL_DMA_ERROR_FE，即DMA FIFO错误，不是Direct Mode错误。**

调用栈确认的路径：

```text
DMA1_Stream0_IRQHandler
    → HAL_DMA_IRQHandler
        → Dcmi_StreamDmaError
            → Dcmi_RecordError(SYS_ERR_IO)
```

这次不是从HAL_DCMI_ErrorCallback进入。中断栈下面曾出现USB_UVC_PayloadBuildPacket，它可能是DMA中断打断主循环时的执行位置，不能单凭调用栈认定USB分包函数逻辑有错。

### 为什么事件错误位后来为0

BSP_DCMI_TakeEvents先把错误复制到events，再清除s_event_dcmi_error和s_event_dma_error。Media保存操作结果，但没有将这两项硬件位长期保存。Stop中的HAL重新初始化也可能清除HAL ErrorCode。

因此，停采后看到两个事件错误位为0，不能排除之前发生过硬件错误。此前建议直接看“锁存变量”却未强调读取后清零，不够准确；后续统一以首次错误现场为准。

### Keil操作要点

1. 在Dcmi_RecordError第一条可执行语句设置断点，重新启动并自由运行到首次命中。
2. 不在采集过程中连续F10/F11，也不反复命中后把后续错误当作首次错误。
3. View → Call Stack + Locals打开调用栈。双击是展开/折叠，不是源码跳转。
4. 右键相应函数，使用Show Caller Code / Show Callee Code定位调用位置。
5. 无作用域的局部变量可能显示cannot evaluate；用Symbols窗口定位静态符号，不要把它当作硬件故障。
6. CPU暂停时DMA/DCMI未必同步停止。不同截图中的NDTR可能变化，不能拼成一份原子现场；断点后的错误与时序不用于证明断点前原因。

## 4. 寄存器与地址核对

|阶段|FCR|CR|NDTR|说明|
|---|---|---|---|---|
|最初错误现场|0xA0|0x00065517|0x140|直接模式；FE中断开启；剩320 Word|
|开启FIFO后仍报错|0xA7|0x00065517|0x1E0|FIFO开启、FULL阈值；剩480 Word|
|后续寄存器截图|0xA7|0x00065517|0x2580|9600 Word即38400B；已不是上一个采样时刻|

CR=0x00065517解码：EN=1、DBM=1、CT=0当前M0、P2M、MINC开启、PSIZE/MSIZE均32位、优先级HIGH、内存突发SINGLE。FIFO错误发生后EN未必由硬件清除，因此BSP后续停止路径仍有必要。

地址截图：PAR=0x48020028，正确指向DCMI->DR；M0AR=0xD0009A00、M1AR=0xD0013000，分别为三缓冲中的1号和2号，满足对齐且不重叠。这个瞬时快照没有明显地址越界，但不能证明所有轮换窗口都正确。

现有布局（结束地址不包含）：

|用途|开始|结束|
|---|---|---|
|保留区|0xD0000000|0xD0000400|
|Buffer0|0xD0000400|0xD0009A00|
|Buffer1|0xD0009A00|0xD0013000|
|Buffer2|0xD0013000|0xD001C600|
|MPU业务窗口上界|—|0xD0020000|

每帧38400B、9600 Word。三缓冲共115200B，起始和步长都满足32B对齐；与之前的SDRAM/MPU范围整改一致。

## 5. 实板对照过程

以下是用户报告的实板观察，不是软件模拟结果；除抓包时间外，没有记录每项测试的精确持续时间，不能据此宣称长期稳定。

|对照场景|用户观察|能够说明什么|
|---|---|---|
|不打开视频取流|稳定|基础采集在这组条件下能运行|
|打开视频，但注释桥接调用|不报错|USB协商、SOF等活动本身不足以复现本次错误|
|恢复桥接，只取帧停在HELD，不Submit|不报错，后续确认确实HELD|持有一个IN_USE缓冲区时，另外两个DMA缓冲区能继续运行|
|HELD中每1ms最多读510B至内部RAM，每100ms循环读一帧，不Submit|触发FIFO错误|新增SDRAM读取/复制负载足以触发；实际USB视频Payload发送不是必要条件|
|设置INCR4后短暂“不报错”，随后正常发送仍报错|后来发现配置被覆盖|不能作为INCR4有效的成功对照证据|

只读测试不是单纯的总线带宽测量：它包含CPU读取、内部RAM写入、计时和主循环负载。尚未区分具体FMC服务延迟、SDRAM时序裕量、DMA FIFO排空效率或其他并发时序问题。

## 6. 关键配置覆盖错误

排查中发现bsp_dcmi.c曾同时出现：

```c
s_dcmi_dma.Init.MemBurst = DMA_MBURST_INC4;
//中间还有其他配置
s_dcmi_dma.Init.MemBurst = DMA_MBURST_SINGLE;
```

HAL_DMA_Init之前，后一条把前一条覆盖，实际仍为SINGLE。因此之前基于“INCR4后短暂稳定”推断主要因素的判断过早，已经撤回。

记录时本地文件已只保留一次MemBurst赋值：

```c
s_dcmi_dma.Init.FIFOMode = DMA_FIFOMODE_ENABLE;
s_dcmi_dma.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_FULL;
s_dcmi_dma.Init.MemBurst = DMA_MBURST_INC4;
s_dcmi_dma.Init.PeriphBurst = DMA_PBURST_SINGLE;
```

**这是本地配置状态，不是已完成最终实板验收的证明。** 下载后检查实际寄存器，而不只检查结构体赋值：

```c
(DMA1_Stream0->CR >> 23) & 3
```

0=SINGLE，1=INCR4。保持PeriphBurst为SINGLE，只验证内存端突发，不同时修改多个变量。

FULL FIFO为4 Word，32位×INCR4为16B；当前缓冲区地址和帧长满足这个组合，当前HAL的DMA_CheckFifoParam也允许此配置。ST AN5020推荐FIFO配合最多4 Word的内存突发以提高性能，但这不是当前问题已解决的证明。

## 7. 目前源码提供的线索

- BSP SDRAM：110MHz、CAS2、ReadBurst关闭、ReadPipe=0；模式寄存器0x0220的CAS也为2，没有发现这两个CAS设置互相矛盾。单独读写曾通过不等于并发裕量已验证。
- OV5640明确调用OV5640_SetPCLK(..., OV5640_PCLK_48M)，XCLK按24MHz配置。宏名不等于实测PCLK。
- USB的10FPS只约束输出节奏，不能证明摄像头也以10FPS输出，更不能用384KB/s平均量代替DVP有效行内瞬时输入速率。
- DMA/DCMI NVIC优先级5，USB优先级6。USB_Device_Lock仅屏蔽OTG_FS_IRQn，没有全局屏蔽DMA/DCMI。
- PayloadBuildPacket逐字节读外部图像并复制到内部包缓冲；它确实增加CPU对SDRAM的访问，但读写不同缓冲区是正常架构能力，不应笼统认定“CPU和DMA不能并发”。
- Media在IN_USE占用时允许继续采集并丢弃后续帧；错误后停止、不自动重启是当前行为。它解释了停采后不再产帧，但不是首次FIFO错误的原因证明。

## 8. 下一轮验收顺序

1. 确认INCR4只有一次赋值，并在实际运行CR中确认MBURST=1。
2. 在同一组条件、足够持续时间下重跑“不读”“持帧”“读取复制”对照，记录首次错误、持续时间及s_read_frames/offset等进度。
3. 若读取复制稳定，再恢复真实USB发送；确认submitted/released持续增长、无采集错误、Windows画面实际正常。
4. 若仍失败，按单变量继续核查输入PCLK/有效数据速率、FMC访问延迟及SDRAM时序裕量；不要同时调整时钟、Burst、MPU或Cache。
5. 保留FE/TE/DME错误检测，不通过清除错误码、强制释放图像或自动重启掩盖故障。
6. 软件测试仅验证状态机/接口契约。最终配置需要重新编译和实板验证，不能把此前软件PASS当作并发硬件通过。

截至本记录：尚无“正确生效的INCR4＋真实相机UVC长时间取流”最终通过证据，也没有确认具体硬件根因。当前正式源码中的临时读取测试已移除；相关调试改动仍在本地未提交。本轮只新增本文档，不提交或改写这些源码。

## 9. 回看源码与资料

- CM7/Application/app_uvc_bridge.c：桥接三态与图像归还。
- CM7/Media/Src/camera_capture.c：CameraCapture_Process、CameraCapture_Stop、CameraStream_Rotate。
- CM7/BSP/bsp_dcmi.c：DMA配置、TakeEvents、StreamDmaError、RecordError。
- CM7/BSP/bsp_sdram.c：FMC、PLL2与SDRAM时序。
- CM7/Service/usb_uvc_payload.c：PayloadBuildPacket图像复制。
- Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_dma.c：FIFO错误检测与XferErrorCallback。
- Tests/SdramRange/verification_report.md：128KB业务窗口、三缓冲布局验收。

[ST AN5020：DCMI DMA FIFO与Burst配置](https://www.st.com/content/st_com/en/technical-documents/an5020-introduction-to-digital-camera-interface-dcmi-for-stm32-mcus-stmicroelectronics_asset/chunk-007-topic-an5020-dcmi-configuration-details.html)

[Keil调用栈窗口](https://www.keil.com/support/man/docs/uv4/uv4_db_dbg_callstack.asp)
