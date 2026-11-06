# Stage03A 软件修复与实板数据进展

分支：codex/camera-jpeg-stage03a，基线c4bd0ad。未合并main。

## 修复及证据

JPEG主动关闭DMA时屏蔽TC/HT，忽略关闭期间迟到完成；真实NDTR=0仍报告容量耗尽。Media按JPEG段结构和熵数据标记确定首个EOI，只交付完整文件前缀。保留SOI/SOF/SOS、尺寸、段边界、容量和硬件错误检查。原测试8/9改为验证尾部不会扩大文件长度；新增Media32直接使用本次真实导出数据。

## 可执行专项

|结果文件|总数|PASS|FAIL|
|---|---:|---:|---:|
|media_test_results.json|33|33|0|
|bsp_test_results.json|32|32|0|
|device_results.json|7|7|0|
|app_results.json|8|8|0|
|export_results.json|9|9|0|

专项总计89/89通过。本轮另重新执行CameraStreamState90/90、CameraContextRefactor72/72、Ov5640Lifecycle32/32、NativeRuntimeStage0297/97。先前同一开发工作区的SDRAM24/24、USB描述符40/40、字符串8/8、EP0 IRQ4/4结果保留，未将其作为本轮重新执行。各命令及逐例结果见commands/results文件。

## 完整重编

命令：python Tests/CameraJpegStage03A/run_build.py --isolated

CM7 TEST、CM7 JPEG、CM7 UVC、CM4全部0 Error、0 Warning。详见build_results.json及四份rebuild.log。新JPEG工程为MDK-ARM/h7_mac_jpeg_stage03a_cm7_jpeg_verify.uvprojx，输出MDK-ARM/h7_mac_STAGE03A_VERIFY_cm7_jpeg/h7_mac_CM7.axf。原正在调试的旧输出未替换。

## 实板数据与限制

Hardware目录保留用户Keil _RBYTE导出的TXT、1104B BIN、1085B JPEG、PNG预览及SHA。JPEG已通过Pillow verify和完整load，160×120，画面很暗。尾部19B的来源尚不明确，导出范围与received是否同轮也未确认；不把尾部称为正常填充。

PC解码真实图像已成功，修正后的固件尚待烧录确认result=0、state=2。重新导出应使用本轮size_bytes，dump_ready.ini提供只读导出。停止/错误恢复及默认YUY2 UVC25FPS仍待实板复测。软件通过不等于全部实板验收完成。

## 变更边界与备份

新增JPEG格式、独立BSP Snapshot收尾、Media可变长度、显式开启的测试应用及独立测试。默认UVC路径不替换。HAL/LL、官方Component、CM4、USB描述符、SDRAM/MPU未修改。用户usb_uvc_desc.h和Keil设置、output/tmp保留，不纳入本任务提交。Keil项目只暂存新增JPEG模块项。

完整实现说明、导出步骤、原始问题复现见README.md和HAL接收审计.md。当前属于软件完成、实板待复验备份，不合并main。
