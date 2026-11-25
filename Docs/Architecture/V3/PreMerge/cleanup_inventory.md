# 清理与保留清单

| 位置 | 内容 |
| --- | --- |
| Core | 移除永远拒绝更新的配置占位接口和测试输出选择。产品能力由正式的 JPEG/Raw 配置决定。栈水位读取作为诊断保留。 |
| Tasks | BoardSmoke 状态机、RecordBoard/Joint 命令邮箱和 ControlTask 测试轮询移到独立测试配置。Supervisor 和唯一 ControlTask 保留。 |
| Services | 清理三服务的模拟分发表、故障注入和专用 Watch 计数。旧命令编号由正式服务显式拒绝，并通过原 Mailbox 发布终结。 |
| Media、Platform、Service | 清理测试协商快照、USB R2 事件环与 Watch 快照。FIFO 占用读取、实际 USB 状态、Reader Proof、硬件首错和 SD 诊断保留。 |
| Keil | 正式 CM7 工程只登记 ApplicationV3 入口和产品配置。保留生产 Supervisor 命令入口与诊断符号。CM4 代码和 Scatter 链接布局沿用冻结版。 |
| Tests | BoardSmoke、RecordBoard 状态机文件和测试头独立归档。与任务私有上下文绑定的诊断和模拟断言由测试覆盖层恢复，覆盖层只应用于临时测试树。 |

保留生产 START 自检使用的 Legacy Get/Release。已有 Legacy Retain/ReleaseLease 契约继续由历史测试覆盖。JPEG 物理块引用、DMA 可替换判据和 Raw 七缓冲均沿用 R1。

### 复现

从包含源码候选的 Git 工作目录运行。需要现有 ARMCC5、Unicorn 和 pyelftools 环境。路径参数应指向新目录。

```powershell
python Tests/ApplicationV3/BoardProfiles/prepare.py output/repro-v3-test
python output/repro-v3-test/Tests/ApplicationV3/run_tests.py output/repro-v3-startup
python output/repro-v3-test/Tests/ApplicationV3/run_tests.py output/repro-v3-joint --joint-board
python output/repro-v3-test/Tests/ApplicationV3/run_tests.py output/repro-v3-camera --board-smoke camera
```

历史 USB/Storage 单服务用 `--board-smoke usb` 或 `--board-smoke storage`，并可加 `--board-stress repeat` 或 `recovery`。独立联合 Keil 编译使用该测试树中的工程和 `APP_VIDEO_MJPEG_MODE=1, APP_V3_RECORD_BOARD_TEST=1, APP_V3_JOINT_BOARD_TEST=1, APP_STORAGE_PARTITION=1`。旧 A4.1 本机未提交入口随 `local/files` 和 `local/working.diff` 保留。历史固件、原始 FAIL 和实板结果均保留在既有输出位置。

### 文件列表

下列清单由 Git 生成。同目录新增 `merge_readiness.md`、`cleanup_inventory.md`、`architecture_audit.md`、`api_changes.md`、`build_verification.md`、`regression_report.md` 和 `known_limitations.md`。

~~~text
M	CM7/ApplicationV3/Core/app_events.c
M	CM7/ApplicationV3/Core/app_events.h
M	CM7/ApplicationV3/Core/video_config.c
M	CM7/ApplicationV3/Core/video_config.h
M	CM7/ApplicationV3/Services/Camera/camera_service.c
M	CM7/ApplicationV3/Services/Camera/camera_service.h
M	CM7/ApplicationV3/Services/Storage/storage_service.c
M	CM7/ApplicationV3/Services/Storage/storage_service.h
M	CM7/ApplicationV3/Services/USB/usb_service.c
M	CM7/ApplicationV3/Services/USB/usb_service.h
M	CM7/ApplicationV3/Tasks/service_tasks.c
M	CM7/Media/Inc/camera_mjpeg_fifo.h
M	CM7/Media/Src/camera_mjpeg_fifo.c
M	CM7/Platform/usb_device.c
M	CM7/Platform/usb_device.h
M	CM7/Service/usb_uvc.h
M	CM7/Service/usb_uvc_class.c
M	MDK-ARM/h7_mac.uvprojx
R100	CM7/ApplicationV3/Tasks/board_smoke.h	Tests/ApplicationV3/BoardProfiles/board_smoke.h
A	Tests/ApplicationV3/BoardProfiles/board_smoke_control.inc
A	Tests/ApplicationV3/BoardProfiles/prepare.py
R100	CM7/ApplicationV3/Tasks/record_board.h	Tests/ApplicationV3/BoardProfiles/record_board.h
A	Tests/ApplicationV3/BoardProfiles/record_board_control.inc
A	Tests/ApplicationV3/BoardProfiles/test_only.patch
A	Tests/ApplicationV3/StorageFs/run_tests.py
A	Tests/ApplicationV3/StorageFs/storage_fs_test.c
M	Tests/ApplicationV3/hardware_fixture.c
M	Tests/ApplicationV3/hardware_fixture.h
M	Tests/ApplicationV3/run_tests.py
M	Tests/ApplicationV3/startup_test.c
~~~
