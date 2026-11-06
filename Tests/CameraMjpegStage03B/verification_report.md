# Stage03B 收尾报告

基线b183ec0，叠加03A收尾e80fdda；独立分支codex/camera-mjpeg-stage03b，未合并main。

## 结果与边界

用户确认Windows MJPEG枚举及OV5640真实连续画面正常；关闭重开、保持独立供电拔插USB、MCU复位后取流均可恢复。03A正常释放停止已实板通过。

实测Camera15.763FPS、USB完整帧发送16.000FPS。25FPS只是目标，尚未达到；主机FPS没有独立测量。不能仅凭两个统计窗口的数值判断具体瓶颈。连续采集时偏暗现象据用户反馈已消失，未专门修改曝光/增益，自动曝光收敛仍属未确认解释。

## 软件执行

|专项|总数|PASS|FAIL|
|---|---:|---:|---:|
|MJPEG Media|16|16|0|
|MJPEG USB|13|13|0|
|MJPEG Native|15|15|0|
|JPEG BSP|42|42|0|
|JPEG Media|39|39|0|
|JPEG Device|7|7|0|
|JPEG App|8|8|0|
|JPEG PC|9|9|0|
|Native02|97|97|0|
|CameraStream|90|90|0|
|CameraContext|72|72|0|
|DeviceLifecycle|32|32|0|
|Descriptors|40|40|0|
|Strings|8|8|0|
|EP0 IRQ|4|4|0|
|Archived Core|204|201|3|
|Archived Payload|124|122|2|

合计执行820，PASS 815，FAIL 5。旧VideoConfig夹具计划28项未执行。

归档Core失败109/110/201要求旧Initcall自动启动USB，与Native Runtime显式启动架构不一致。Payload在O0/O2的case37要求513B非法，而b183ec0已支持1023B。VideoConfig夹具引用已移除的App_UvcBridgeProcess并缺少消息桩。没有删除用例、修改断言或把这些失败计为通过。

YUY2随机状态机初跑2项在编译并发时触及45秒时限，编译结束后原样重跑90/90通过，原时限和断言均未改变。

## 五配置完整重编

CM7_MJPEG、CM7_YUY2、CM7_TEST、CM7_JPEG_SINGLE、CM4均0 Error、0 Warning。build_results.json包含各次编译前后源文件SHA；收尾已核对五份记录与当前源码完全一致。

## 实现和审计

JPEG连续视频复用03A安全Snapshot收尾，按各缓冲槽valid_bytes交付实际文件长度。五个600KiB槽保留DMA/READY/IN_USE/FREE规则，JPEG每次DMA最多65535Word，不套用固定帧长DBM，不覆盖USB借用槽。CameraTask和UsbTask仍独占各自任务侧API。

沿用FrameRequest/Offer/Return/ReturnAck及epoch/lease，新增FRAME_CANCEL_REQUEST取消尚无租借的待决请求，解决Stop等待USB、USB等待下一帧的循环等待；已有租借仍必须Return/ACK，队列满保留状态重试。

UVC1.1、34B Probe/Commit、原SOF/Payload/FID/EOF/Token、EP1 ISO1023复用。MJPEG描述符158B，静态160B对齐；最大输出帧38400B，实际每帧长度单独提交。冷启动选择MJPEG，默认YUY2路径不变。

HAL/LL、官方ST Core、OV5640 Component、CM4、USB BSP/Core、UVC Payload和USB Mode源码未修改。源审计确认10个原固定帧长DMA/Media轮换函数文本不变。

## 尚未完成的验收

- 25FPS目标与主机实际接收FPS。
- ControlTask命令级Stop/Start和强制错误恢复的实板证据；软件注入不是硬件验证。
- 归档回归5失败、28未执行。

因此不宣称03B全项验收或全仓回归全部通过。本轮已证明真实MJPEG连续显示、列明的恢复场景及五配置编译。

## 本地收尾

成果保留在远端开发分支，未经批准不合并main。清理临时验收工程、输出、RTE/DebugConfig副产物后切回main；原用户Keil/头文件修改完整备份，恢复时仅剔除03A临时源文件条目，防止main引用不存在的文件。

本地main仍是c4bd0ad的原YUY2主线，不包含本次MJPEG实现。原始JPEG、专项测试和完整记录保存在远端分支。
