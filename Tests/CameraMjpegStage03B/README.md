> JPEG验收入口清理：正式工程不再支持APP_CAMERA_JPEG_TEST_ENABLED或CM7_JPEG_SINGLE镜像，旧选择会明确拒绝；正常Snapshot API与MJPEG路径保留。见[清理报告](../CameraJpegStage03A/cleanup_report.md)。

> 维护说明（2026-11-08）：本目录中已确认不兼容的旧测试入口已按用户要求删除；其余公共桩、共享C夹具及历史报告保留。下文为历史说明，当前可运行集合以 Tests/SdmmcStage08B/run_regressions.py 为准。删除不代表旧失败被修复。

# Stage03B：OV5640 连续JPEG与USB UVC MJPEG

## 范围和基线

以b183ec0为03A基线，叠加03A停止恢复收尾e80fdda，在codex/camera-mjpeg-stage03b开发，未合并main。160×120 MJPEG目标25FPS；默认YUY2五缓冲25FPS配置不变，不进行运行时格式热切换。

## 接收及所有权

JPEG流采用连续重复的Snapshot，而不是固定字数DMA DBM：每轮沿用03A FRAME、CAPTURE、FIFO空、EN清零、NDTR及HAL恢复检查，在CameraTask解析段结构与EOI，记录该槽valid_bytes；随后只选择FREE槽重启下一轮。每次最多65535Word（262140B），不把TC或FRAME单独作为成功依据。不承诺覆盖传感器输出的每一个物理帧，实际采集FPS待测。

五个槽各预留614400B，地址与YUY2相同；JPEG每次DMA只使用槽内262140B。DMA、READY、IN_USE、FREE状态保留。最多一个JPEG DMA目标、一个最新READY和一个消费者IN_USE。新图像替换无人持有的旧READY，不能覆盖IN_USE。JPEG字段不会参与YUY2原M0/M1算法。

CameraTask唯一操作Device/Media，UsbTask唯一操作UVC。任务间沿用FrameRequest/Offer/Return/ReturnAck及epoch/lease，不复制图像。补充FRAME_CANCEL_REQUEST用于取消尚未取得帧的请求：Camera返回无租借的失败Offer确认；若已借出则仍必须走Return/ACK。队列满保留状态重试。

正常Stop遇到IN_USE先返回BUSY；故障Stop先停硬件，再进入DRAINING，归还后才结束模式。Abort失败保留USB所有权和Media借用，不能超时强制释放。

## 冷启动配置和USB

业务配置仍只在CM7/Application/video_config.c中选择，编译APP_VIDEO_MJPEG_MODE=1启用JPEG。output_fps=25、output_max_frame_bytes=38400：这是允许传输的JPEG单帧上限，不是实际帧长度，也不是传感器FPS。超过上限的JPEG被拒绝提交并安全归还，不截断发送。

MJPEG独立配置描述符158B：Configuration9+IAD8+VC接口9+VC专用52+VS接口9+VS专用55+Alt1接口9+EP7。VS专用55=InputHeader14+MJPEGFormat11+MJPEGFrame30。VC拓扑、UVC1.1、EP1 IN ISO1023不变。MJPEGFormat子类型6，Frame子类型7，可变长样本标志为0。静态数组160B、4字节对齐，158B有效长度与PIO尾部存储分开。

Probe/Commit仍为34B，dwMaxVideoFrameSize=38400是上限；每次SubmitFrame使用该帧实际长度。保持现有Token、SOF、FID/EOF和取消确认。每SOF最多提交一包；25FPS间隔40SOF，最大38400B需38包，软件预算允许，但真实吞吐仍须测量。

字段核对参考Linux官方UVC1.1定义（MJPEG Payload 3.1.1/3.1.2）：https://raw.githubusercontent.com/torvalds/linux/master/include/uapi/linux/usb/video.h 。USB-IF旧1.1 Payload PDF链接未能获取，本轮不声称逐页读过原文。

## 构建和烧录

只使用一份固定验收工程MDK-ARM/h7_mac_stage03b.uvprojx。运行`python Tests/CameraMjpegStage03B/run_build.py --prepare-only`可重新生成；业务源码始终是当前工程，不复制源码快照。

Target：CM7_MJPEG、CM7_YUY2、CM7_TEST、CM7_JPEG_SINGLE、CM4。构建结果统一在output/build_stage03b，不在MDK-ARM生成多组临时目录。正式h7_mac.uvprojx和用户Keil设置不覆盖。

烧录CM7_MJPEG时镜像为output/build_stage03b/CM7_MJPEG/h7_mac_CM7.axf；CM4保持原OLED固件。先断开USB主机线，烧录后复位运行再插线。不要把CM7_JPEG_SINGLE当作MJPEG视频固件。

## Windows验收步骤

1. `ffmpeg -hide_banner -f dshow -list_options true -i video="Roxy Camera"`，必须看到MJPEG、160x120、25FPS能力；声明值不是实测FPS。
2. `ffplay -f dshow -video_size 160x120 -framerate 25 -vcodec mjpeg -i video="Roxy Camera"`。先关闭占用设备的其他程序。
3. `ffmpeg -hide_banner -f dshow -video_size 160x120 -framerate 25 -vcodec mjpeg -i video="Roxy Camera" -t 30 -an -c:v copy output/mjpeg_real.avi`，保留日志与真实视频；再用PC完整解码检查。
4. Watch分别观察g_camera_task_debug.measured_fps_milli、g_usb_task_debug.measured_fps_milli及主机接收FPS，分开记录，不把25配置值当作实测值。
5. 观察g_bridge_debug.acquired_frames/submitted_frames/released_frames、s_capture.stream各槽状态及valid_bytes。USB在途借用最多一帧，停止后acquired与released应闭合。
6. 分别测试Alt0/Alt1、USB拔插、Reset、ControlTask Stop/Start和错误恢复，不能根据软件模拟结果勾选硬件通过。

TEST/YUY2与MJPEG沿用既有设备身份。若Windows仍只列YUY2，先确认烧录Target并重新插拔；必要时在设备管理器移除该测试设备后重新枚举，不擅自换用其他VID/PID。

## 验收边界

03A真实1113B JPEG已完整解码，正常释放停止已实板通过。03B测试中仅在独立夹具注入该样本，正式固件没有生成或循环发送假JPEG。Windows连续真实MJPEG、三端FPS、Stop/Restart及故障恢复的实板结果尚待用户提供。偏暗记录单独在Docs/Debug/OV5640_JPEG偏暗排查记录.md，不阻塞视频链路。

## 实板反馈更新

用户在烧录CM7_MJPEG并按枚举、取流步骤操作后反馈“一切符合预期”。据此记录Windows MJPEG枚举及真实连续显示通过。25仍为配置帧率；Camera、USB、主机FPS尚未取得实测数值。Reset/Disconnect/Alt0/Stop/Restart与强制错误恢复仍逐项保留待实板验收，不能从正常显示推导为已通过。

## 历史回归及当前限制

旧USB Core归档204项执行201通过、3失败（109、110、201），断言要求旧Initcall自动启动USB，与Native Runtime显式启动架构不一致。Payload归档124次执行122通过、2失败（O0/O2的case37），该用例仍要求513B非法，而b183ec0已经支持1023B。旧VideoConfig夹具引用已移除的App_UvcBridgeProcess并缺少App_Messages桩，链接失败。未修改这些历史断言，也未把它们计为通过。YUY2随机状态机最初2项在编译并发时触及45秒仿真时限，编译结束后原样重跑90/90通过。

当前最终MJPEG镜像已完成源文件SHA稳定性检查及0 Error/0 Warning完整重编。其他配置首轮均0/0；收尾过程中的最终重编被交互中断，未全部获得带最终SHA的结果，仍需补齐。不能将本记录解释为全部软件及硬件验收已经完成。

旧03A临时工程及界面状态已经删除。单帧验收统一使用固定03B工程的CM7_JPEG_SINGLE Target；真实数据和历史报告保留。

## 最新收尾结论

以verification_report.md、final_results.json及hardware_acceptance.json为准：五配置最新源码完整重编0/0；Windows真实MJPEG和三项恢复实板通过。Camera15.763FPS、USB16FPS，25FPS尚未达到；主机FPS、命令级Stop/Start及强制错误恢复待测。此前“仍需补齐最终构建”的内容属于历史进度。
