# MJPEG Continuous：独立SDRAM字节FIFO

最新实板反馈：Continuous FIFO、QVGA已通过；VGA功能通过但未达10FPS目标。见`Tests/CameraMjpegStage03B/hardware_status_summary.md`，不移用旧Snapshot版本的约16FPS。

当前VGA测试配置：MJPEG 640×480、目标10FPS、最大帧70000B，见`Tests/MjpegVga/README.md`。320×240仍支持；QVGA阶段历史记录见`Tests/MjpegQvga/README.md`。下文为FIFO最初接入记录。

本轮基线：`codex/camera-mjpeg-stage03b` 的 `60c94d0`，即撤销旧接收环后的分支。
以下记录为最初源码交付时状态；后续用户实板结果见本文顶部链接。未合并main。按用户要求，本轮不运行C编译器、Keil或可执行测试；编译和实板测试由用户完成。

## 保留的功能

- YUY2仍使用原五块600KiB缓冲、原TC/FRAME配对和原地址轮换。
- JPEG Snapshot仍走`BSP_DCMI_StartJpegSnapshot`和原单帧结构验证。
- MJPEG Stream改用独立字节FIFO，不再循环启动Snapshot，不在正常帧间关闭CAPTURE。
- FrameRequest / FrameOffer / FrameReturn / ReturnAck及外部stream_epoch / lease_id校验不变。
- USB描述符、Probe/Commit、SOF、Token及Abort完成确认协议不改。

## SDRAM地址

所有结束地址均不包含自身。沿用现有4MiB MPU Normal、Non-cacheable、Shareable窗口，不修改MPU、FMC或SDRAM初始化。

| 用途 | 范围 | 大小 |
|---|---|---:|
| 原YUY2五缓冲 | 0xD0000400～0xD02EE400 | 5×614400B |
| MJPEG字节FIFO | 0xD0300000～0xD0380000 | 512KiB |
| M0丢弃区 | 0xD0380000～0xD0382000 | 8KiB |
| M1丢弃区 | 0xD0382000～0xD0384000 | 8KiB |
| 业务窗口结束 | 0xD0400000 | 4MiB |

FIFO分64块，每块8KiB；DMA每目标2048 Word。块与JPEG帧无一一对应关系。原五缓冲不缩小、不复用为MJPEG接收区。

## 调用链

```text
CameraTask
  → App_CameraInitStream
  → CameraCapture_StartStream
      JPEG → CameraMjpegFifo_Start
           → BSP_DCMI_StartJpegContinuous
           → DMA DBM + DCMI Continuous

DMA TC IRQ
  → Dcmi_ByteM0Complete / Dcmi_ByteM1Complete
  → Media登记SEALED块、保留下一块
  → BSP检查CT/NDTR、替换非活动目标、复核窗口
  → 唤醒CameraTask

DCMI FRAME IRQ
  → 登记输入边界并恢复FRAME中断
  → 唤醒CameraTask，不扫描JPEG、不暂停CAPTURE

CameraTask
  → CameraCapture_Process
  → 先检查BSP错误
  → CameraMjpegFifo_Process
  → 解析已完成块，生成READY帧描述
  → 再检查BSP错误
  → 原Offer/Return/ACK租借协议

UsbTask
  → USB_UVC_SubmitFrame / USB_UVC_SubmitFrameView
  → 原Payload、SOF及EP1异步发送
  → 确认frame_owned=false后Return
  → CameraTask实际释放块引用后ReturnAck
```

## 所有权

块状态：`FREE → DMA → SEALED → FREE`。

`SEALED`必须同时满足“已解析”和“引用数为0”，才能回到FREE。块可以同时包含多张JPEG；每个READY描述、唯一借出描述和当前解析描述分别持有引用，不能只用一个occupied布尔值。

帧描述保存FIFO偏移、真实字节数、块位图和内部epoch。内部epoch只标记字节缺口，与跨任务租借身份不是同一个计数器。

正常停止遇到借出帧，Media返回BUSY，等待消费者归还。错误停止先停止硬件，Stop成功后丢弃接收端和READY，但保留借出帧，进入DRAINING。Stop失败不清空引用、不重置接收端。

归还先核对全部块引用，再执行递减；不能先减一半后失败重试，导致重复释放。

## 无整帧搬运

一张JPEG跨FIFO尾部时，使用两个只读段：

- `CameraFrame_t.data`是第一段，长度为`size_bytes - size_bytes2`。
- `data2`是第二段，长度为`size_bytes2`。
- `size_bytes`始终为整张JPEG的真实总长度，不含尾部Word填充。

桥接层只传递描述。UVC新增`USB_UVC_SubmitFrameView()`，原连续内存接口保留。Payload跨段取字节，只有真实完成确认才推进偏移和FID。

这里消除的是整帧搬运。现有USB PIO发送仍需把每个UVC小包的数据和Header放入包缓冲，不宣称CPU完全不复制任何字节。

## 解析与交付条件

任务只读取TC确认完成、已退出活动DMA目标的块，不利用活动NDTR估算可读字节。换址前后仍检查CT、NDTR余量、EN/DBM、目标地址和新TC挂起状态；失败锁存错误并交给任务Stop。

增量解析器跳过APP/DQT/DHT等段内容，检查Baseline SOF尺寸、SOS、熵数据中的FF00和RST，以及结构EOI；APP内部的FFD9不是帧尾。只有完整结构、同一无缺口epoch和累计输入FRAME证据共同满足时才发布描述。块TC不与FRAME逐个配对，因为一块可能含多帧。

这是结构完整性检查，不是JPEG完整解码器。PC解码仍是实板验收的一部分。当前Media单帧上限262140B；本轮不改变既有UVC最大帧声明38400B，超出USB声明的帧由提交接口拒绝，经原归还协议释放，不截断为可发送图像。

## 压力处理

READY队列容量8。满时只丢最老且未借出的READY。若DMA顺序上的下一块仍被解析器、READY或USB持有，改写该目标专属丢弃区，并增加内部epoch；正常CAPTURE不停止。

任务看到换代后丢弃未完成图像和未借出READY，随后从新SOI重新同步。已经借出的图像保持不变。丢弃块数不等于准确的传感器丢帧数，不把它混入completed_frames。

## 延迟与风险

- 本轮未验证此前DMA FIFO错误已经消失。8KiB、FIFO FULL、MemBurst INC4、PeriphBurst SINGLE是待实板验证配置，不能据此认定历史FE根因已解决。
- 只解析完整块意味着小JPEG可能等待后续图像填满8KiB才交付。连续采集不暂停，但交付有块填充延迟和批量特征；不能用配置25FPS声称实测25FPS。
- 停止时尚未TC完成的块不发布，最后一帧可能被丢弃，不为交付尾帧临时暂停或排空正常流。
- SDRAM高地址、DMA窗口余量、FIFO突发访问、USB并发和长时间借用必须实板验证。
- CameraTask内做有限预算解析；保留现有进度事件和任务等待策略，没有引入新的RTOS任务或跨层任务句柄。

## 编译与实板步骤

使用正式`MDK-ARM/h7_mac.uvprojx`，不是旧临时工程或旧AXF。新模块已加入CM7 Media Group；CM4中的该Group仍排除构建。用户本地的工程选项和调试设置保留。

1. MJPEG：CM7全局Define设`APP_VIDEO_MJPEG_MODE=1`，`APP_VIDEO_TEST_MODE=0`，`APP_CAMERA_JPEG_TEST_ENABLED=0`；完整重编CM7。
2. JPEG单帧：使用原单帧验收开关，不同时启动Stream；不得拿旧Stream镜像替代单帧镜像。
3. YUY2：`APP_VIDEO_MJPEG_MODE=0`，完整重编；不改原五缓冲。
4. CM4按原工程构建和烧录，不改其源码。
5. 先确认Camera格式为JPEG、Stream启动结果0、USB正确枚举MJPEG，再打开视频。
6. 观察`g_camera_task_debug.result`、`g_usb_task_debug.result`、`g_control_task_debug.last_fault`和桥接提交/归还计数。
7. 测量Camera、USB和主机实际FPS；验证长时间运行、Alt0/Alt1、拔插、Reset、Stop/Restart及Abort失败时借用不提前回收。
8. 若再报DMA错误，记录首次错误的ErrorCode、FCR、CR、NDTR及M0AR/M1AR，不把调试暂停后的寄存器当作首次错误现场。

## 本轮检查状态

实际只运行`Tests/MjpegSdramFifo/static_audit.py`源码检查，结果见同目录`static_results.json`。C99语法树检查使用占位类型，不具备真实头文件类型检查、链接或ARMCC兼容性验证能力。

已准备14项PC隔离C测试，未编译、未执行。旧03B的重复Snapshot流测试依赖已移除的`jpeg_index`和五缓冲JPEG内部实现，不能直接代表新FIFO验收；原历史报告不改写成新版本PASS。
