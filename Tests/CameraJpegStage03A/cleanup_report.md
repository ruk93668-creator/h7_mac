# JPEG历史验收应用从正式源码清理

日期：2026-11-08。在codex/stage08b-sdmmc-idma完成，不合并main。

## 正式工程

- 将CM7/Application/app_camera_jpeg.c/.h原样移到Tests/CameraJpegStage03A/acceptance，不删除独立验收逻辑。
- camera_task.c移除该头文件和五处APP_CAMERA_JPEG_TEST_ENABLED条件分支；保留原来宏为0时的完整业务路径，没有改变命令/ACK、BOOT、Media处理和状态发布。
- Keil两核Target均移除该C/H登记。CM4中原来已排除的展示项也删除，不修改CM4源码和实际运行路径。
- 正式CM7/CM4/Common源码和Keil工程已无该宏、App_CameraJpegProcess或g_camera_jpeg_test引用。不能再用旧宏切换正常CameraTask。

## 保留单帧和MJPEG

App_CameraConfigure、App_CameraInit、App_CameraRun/Stop、CameraCapture_Start/Process/GetFrame/ReleaseFrame/Stop和BSP_DCMI_StartJpegSnapshot仍保留。JPEG配置与SOI/EOI解析、Snapshot停止恢复、Device生命周期均未改。

BSP、Device、Media、USB UVC/Mode/Platform、JPEG Continuous FIFO和视频默认配置与清理前一致。单帧调用仍必须遵守CameraTask资源所有权及Snapshot/Stream互斥规则，没有新增应用触发接口。正常MJPEG不是通过被删除的历史验收宏启动。

## 独立测试与兼容入口

app_test.c改为引用acceptance/app_camera_jpeg.c。补充Application头文件路径，并链接真实camera_mjpeg_fifo.c，解决历史夹具漏掉后来新增FIFO依赖的问题。fifo_dependency.inc仅Mock连续采集硬件启动和测试内存屏障，不产生图像或完成事件；Snapshot数据完整性仍由真实Media解析及原断言验证，不等于硬件验收。

允许独立测试链接ARMCC既有运行库以提供FIFO所需64位移位辅助函数，没有复制或更改FIFO算法。强化了调用次数检查：应用和Device单帧用例不能误启动连续采集；Media已有模式互斥用例16允许一次连续启动，其余为0。原断言未删除或放宽。

旧CM7_JPEG_SINGLE镜像变体不再生成；run_build.py会在创建工程前明确拒绝，避免传入宏后编译了正常业务却误称单帧验收通过。历史烧录/Watch说明仅对应旧提交。当前保留独立软件入口：

```text
python Tests/CameraJpegStage03A/run_app.py
python Tests/CameraJpegStage03A/run_device.py
python Tests/CameraJpegStage03A/run_media.py media_test --count 39
python Tests/CameraJpegStage03A/run_bsp.py bsp_test --count 42
```

## 实际结果

Application8/8、Device7/7、Media39/39、BSP42/42、MJPEG USB13/13、Native USB11/11，总计120/120通过。范围/链接检查10/10通过，确认迁移文件字节相同、CameraTask正常路径相同、底层Snapshot/MJPEG源码未动及旧验收上下文未链接。

CM7/CM4现有正式Target增量构建均0 Error、0 Warning。没有额外生成测试Keil工程，没有完整重跑全仓历史集合，也没有重新实板验证视频或JPEG单帧。结果见cleanup_verification.json和两核构建日志。

重新打开原h7_mac.uvprojx，正常固件不再提供g_camera_jpeg_test，也不再接受历史JPEG单帧验收宏作为CameraTask分支。待用户烧录确认正常视频，不能把软件PASS写成实板MJPEG通过。
