# UVC三缓冲吞吐受限与五缓冲优化排查记录

记录日期：2026-11-05。只记录本次吞吐问题，不混入WFI枚举、OLED或DMA FIFO故障。

## 面试60～90秒复述

> 我把相机视频链路迁到FreeRTOS原生任务后，配置目标是160×120 YUY2、25 FPS，但播放端的主时钟和视频时钟差持续增大。当时我不知道是相机、USB还是跨任务交付的问题，所以先用FFmpeg实测，发现电脑实际接收只有15 FPS。再比较设备计数，相机约31 FPS，USB整帧完成约15 FPS，说明相机产帧足够，问题集中在采集到发送之间。我检查桥接的获取、提交和归还计数，数量基本平衡，状态也在申请图像和USB持有之间切换，没有明显卡死。随后加入阶段耗时统计，测到等图像约25ms、USB借用约40ms、归还约0～1ms，一轮正好约65ms，和15 FPS吻合。回到源码才看清：三缓冲中两块给DMA、一块被USB持有，无法同时准备下一张可交付图像，发送与等待串行。我扩成五缓冲，分开管理READY和IN_USE，只保留最新完整帧，继续保护借用帧；软件所有权在BSP确认地址替换成功后才提交。模拟先从15提升到25 FPS，之后实板也确认25 FPS且运行稳定，播放端时钟差降到0～15ms。这个过程让我认识到，计数平衡只能证明没有明显积压，要定位吞吐瓶颈，还需要测阶段耗时和检查缓冲所有权。

## 现场与影响

链路：OV5640 → DCMI/DMA → Media → CameraTask → 帧租借 → UsbTask → UVC → Windows。

- 实际图像配置：160×120、YUV422，USB侧对应YUY2，每帧38400B。
- 配置输出目标：25 FPS；这个配置值起初不是实测结果。
- 问题不是完全没有图像，而是持续输出约15 FPS，ffplay的M-V逐步累积到秒级。
- 当时采用三缓冲，两块为DMA目标，一块供应用读取。最初尚未确认这是原因。

## 实际排查时间线

| 顺序 | 当时的疑问 | 动作和证据 | 能得出的结论与转向 |
|---|---|---|---|
| 1 | ffplay显示的6.040、6.115是不是FPS？ | 先辨认字段：这是M-V，单位秒；fd是播放器丢帧计数 | 不能用M-V当帧率，先测实际输入帧速率 |
| 2 | 实际有没有达到配置的25 FPS？ | 关闭播放器后用FFmpeg接收并丢弃输出，持续观察fps；数值稳定约15 | 25只是申请值，实际吞吐约15 FPS |
| 3 | 是相机产帧不足，还是后面没及时发送？ | Camera measured_fps_milli=30969，USB=15000，电脑端也约15 FPS | 相机侧约31 FPS；集中检查图像交付到USB完成这一段，尚不能单凭计数定位具体函数 |
| 4 | 是否只是丢掉了31−15≈16帧？ | 观察Media dropped_frames，反馈每秒约增加7～8 | 不能直接用不同窗口的统计相减；没有据此强行证明“一半帧都被这个计数统计到”，继续检查实际等待 |
| 5 | 帧是否被借出后不归还，或任务队列堵住？ | acquired/submitted/released基本一致；状态在REQUEST和LOANED间切换 | 没有明显长期租借积压；状态瞬时观察仍不能区分每阶段耗时 |
| 6 | 每帧约66ms主要花在哪里？ | 在UsbTask的状态转换处记录毫秒Tick，得到REQUEST约25ms、USB借用约40ms、RETURN约0～1ms | 三段合计约65～66ms，与15 FPS吻合，等待图像没有与发送充分重叠 |
| 7 | 为什么相机约31 FPS，却需要在发送后再等图？ | 阅读Media轮换、Get、Release及桥接Request/ReturnAck路径 | 两块DMA加一块IN_USE占满三缓冲；无法同时保留READY。归还后需要等新的完整帧交付 |
| 8 | 能否增加并行准备空间，同时守住所有权？ | 改为五缓冲，分开READY/IN_USE索引，保留最新READY，原BSP切换与租借凭据协议不变 | 当前帧发送时可以准备下一帧；不是通过提前释放USB帧或复制整帧绕过问题 |
| 9 | 软件与实板是否都改善？ | 专项与集成测试通过；模拟33ms相机源中EOF从15提升到25；随后用户确认实板25 FPS通过、运行稳定 | 当前配置下吞吐目标通过；M-V改善是辅助证据，不是FPS或端到端延迟测量 |

第4步的7～8来自当时人工观察，没有同一窗口、同一时刻的完整计数快照；不为它补造精确的帧数守恒关系。

## 测量命令与字段

关闭ffplay及其他占用设备的程序后，在PowerShell使用：

```powershell
ffmpeg -hide_banner -stats_period 1 -f dshow -video_size 160x120 -framerate 25 -pixel_format yuyv422 -i video="Roxy Camera" -an -fps_mode passthrough -f null NUL
```

设备名称以当时系统实际枚举名称为准，不同时打开多个取流程序。等待帧率稳定后观察，按q退出。

- `-framerate 25`：请求输入格式，不是实际帧率证明。
- `fps=`：FFmpeg本次运行按实际时间统计的平均处理速率；这里轻量接收并丢弃输出，用来观察电脑端吞吐。
- `frame=`：累计处理帧数。
- `time=`：媒体时间戳，不是程序实际运行时长。之前510帧对应20.40秒，正好按25 FPS时间戳推进，但实际接收速率约15，不能据此算成25 FPS。
- ffplay的`M-V`：主时钟减视频时钟，单位秒，不是FPS。
- ffplay的`fd=0`：播放器统计未丢帧，不表示Media没有主动淘汰旧READY。
- M-V接近0不证明摄像头到屏幕只有几毫秒，也不能证明传感器内部曝光延迟。

## 关键Watch证据

```c
g_camera_task_debug.measured_fps_milli
g_usb_task_debug.measured_fps_milli
g_camera_task_debug.dropped_frames

g_bridge_debug.acquired_frames
g_bridge_debug.submitted_frames
g_bridge_debug.released_frames
g_bridge_debug.usb_queue_retries
g_bridge_debug.usb_rejected_messages

g_bridge_debug.request_wait_ms
g_bridge_debug.usb_owned_ms
g_bridge_debug.return_wait_ms
```

FPS字段除以1000。Camera统计Media观察到的完整帧进展；USB统计真实EOF完成，均不等于Windows已经显示的帧数。

耗时截图中的十进制记录：

| 阶段 | 最近值 | 历史最大值 | 说明 |
|---|---:|---:|---|
| REQUEST等待 | 25ms | 26ms | 建立请求到接收有效FrameOffer，含任务与队列等待 |
| USB借用 | 40ms | 61ms | Submit成功到确认USB不再持帧，含发送、调度、取消/恢复等等待 |
| RETURN确认 | 0ms | 1ms | 进入归还阶段到匹配的成功ReturnAck |

截图同时显示submitted=351、released=350，消息拒绝和队列重试为0。允许一帧在途，计数不完全相等不等于泄漏。最近样本可来自相邻帧，Watch也不是原子快照；不能把三个历史最大值相加当作某一帧耗时。

这些耗时字段属于当时工作区新增的诊断代码，当前仍作为独立未提交修改保留；五缓冲提交没有混入这部分代码。检查纯仓库版本时若没有字段，不能认为Watch失败就是业务故障。

## 源码怎样印证这个等待

旧流程：

```text
DMA M0 + DMA M1 + USB持有IN_USE
                 ↓
       没有额外空间保留下一张READY
                 ↓
USB完成 → Return → Camera释放 → ReturnAck
                 ↓
      新Request等待可交付完整图像
```

桥接只在USB可接受图像时发请求；USB借用期间禁止释放Media图像；收到有效ReturnAck后才结束凭据。这些保护本身正确，优化不能破坏它们。

原`consumer_index`只能表示一张READY或一张IN_USE，不能同时表达两者。单纯把数量宏改成5不会自动解决问题，必须同步修改所有权表达和轮换规则。

新流程：

```text
DMA M0 + DMA M1 + USB持有IN_USE + 最新READY + 空闲余量
                         │             │
                         └──并行准备───┘
USB完成并归还 → 获取已经准备好的READY → 下一帧提交
```

采用最新帧策略：同一时刻最多一张READY、一张IN_USE。不积累视频帧队列，避免采集快于输出时画面延迟不断增加。4块已经提供关键并行空间；选择5块是增加余量，不代表只有5块才能改善。

关键文件和函数：

| 文件 | 核对位置 |
|---|---|
| `CM7/Media/Src/camera_capture.c` | CameraStream_Init、OwnershipValid、FindReplaceable、Rotate、GetFrame、ReleaseFrame |
| `CM7/Application/app_uvc_bridge.c` | App_UvcBridgeUsbProcess、Bridge_UsbReturn及Camera归还确认 |
| `CM7/Service/usb_uvc_class.c` | USB_UVC_Process中SOF机会、完成确认与帧节奏 |
| `CM7/BSP/bsp_sdram.h` | 业务窗口及结束地址 |
| `CM7/Core/Src/main.c` | MPU_ConfigSdram与窗口一致性断言 |

## 修改边界和内存

- 单块按640×480×2预留614400B，五块共3072000B；这是后续开发余量。
- 当前采集和USB仍为160×120、YUY2、38400B/帧、目标25 FPS。
- 当前DMA仍为9600 Word/帧；没有实现640×480采集或DMA分段。
- 首1KiB保留，五块从0xD0000400开始，结束地址为0xD02EE400，不包含末地址。
- SDRAM业务窗口及MPU Region2由128KiB同步为4MiB，原Normal、Non-cacheable、Shareable、可读写、禁止执行属性保持。
- 不改BSP的TC/FRAME配对、CT/NDTR切换保护，不改UVC描述符、Probe/Commit或USB包调度。
- BSP地址替换失败不提前提交Media状态；归还只释放IN_USE，不清掉已经准备好的READY。

## 验收与当前结论

| 层次 | 结果 | 证据来源 |
|---|---|---|
| 软件专项与回归 | 341次执行PASS，包含重复运行的字符串检查 | 五缓冲实施时保存的测试结果 |
| 编译 | CM7 TEST、CM7 UVC、CM4均0 Error、0 Warning | ARMCC5完整重编日志 |
| 模拟吞吐 | 固定33ms产帧源、1秒模拟中，USB EOF从15提升到25 | Native Runtime集成测试；不代表硬件 |
| 实板输出 | 用户确认25 FPS通过，运行稳定 | 本次实板测试反馈；助手未直接操作硬件 |
| 播放时钟差 | M-V由累积至秒级改善为约0.000～0.015秒 | 用户提供的ffplay运行输出 |

测试与实现详情见[五缓冲验收记录](../../Tests/CameraFiveBuffers/README.md)。代码提交为`6b0b1c1`；本篇是其后的实板反馈补充，不把原“未实板验证”的历史报告改写成当时已经验证。

目前没有提供五缓冲后完整原始FPS日志、精确稳定运行时长或掉线重连压力测试记录，所以不宣称完成长时间老化、所有错误恢复场景或整个4MiB SDRAM逐地址验收。实际帧长仍为38400B，高地址缓冲参与采集也不等于每块600KiB预留空间都已读写验证。

## 回顾要点

1. 先确认字段含义，再确认真正的实际吞吐。
2. 用采集、USB完成、主机接收三组统计缩小范围，不能只看配置值。
3. 数量平衡不代表速度足够；状态持续变化也可能每轮都在等待。
4. 用阶段耗时解释15 FPS，再用源码中的缓冲所有权验证原因。
5. 优化的是流水线重叠，不能以提前释放、覆盖借用帧或放宽DMA校验换取速度。
6. 软件模拟、用户实板反馈、原始日志和尚未验证的边界分开记录。
