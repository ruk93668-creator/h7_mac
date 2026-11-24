# ApplicationV3 主工作目录迁移及原 Keil CM7 接入

## 迁移与保护

正式工作目录为 `D:/32/STM32H7/h7_mac`，分支为 `codex/application-v3-flipper`，接入基线为 `6315148b7baf2ec88e4c263e793d2bc72cd93a88`。旧清理分支仍指向 `e0ad3b5`，main 仍指向 `f0ce45f`。

全量复制在用户明确缩小范围后中止，已复制部分保留。切换使用经 SHA256 校验的精简备份。主目录备份 5 个文件，旧 V3 工作树备份 3 个文件。备份包含原文件、staged/unstaged binary patch、原始路径映射和 Git 状态。历史 output、原未跟踪文档和此前的实板证据留在原位。已有构建目录另在本仓库内归档，然后直接从原 Keil 工程 Rebuild。 [精简备份](D:/32/STM32H7/h7_mac/output/ApplicationV3/BranchSwitchBackup/20261120T142050Z-conflicts) · [旧构建归档](D:/32/STM32H7/h7_mac/output/ApplicationV3/KeilIntegration/20261120T142050Z-conflicts/PreBuild)

旧 V3 工作树只在同一 HEAD 执行 detach，原两处未提交 Keil 修改经 binary patch 比对一致。主目录只恢复已备份且确实阻塞切换的 `v2_types.h` 和 `h7_mac.uvprojx`，本地 `h7_mac.uvoptx` 保持用户原字节。该本地文件未进入接入提交。备份、构建和正式验证数据受 Git 忽略规则保护。

## 原工程的编译期选择

继续打开原 [h7_mac.uvprojx](D:/32/STM32H7/h7_mac/MDK-ARM/h7_mac.uvprojx)，选择 `h7_mac_CM7`。该 Target 的 C 预处理宏包含 `APP_V3_BUILD=1`，此时应用初始化入口与四个服务任务进入 V3。没有新增 Target，也没有创建工程副本。原双核文件清单保持一致，CM4 整个 Target 定义按字节保持原样。

- [原 app_main.c 入口槽](D:/32/STM32H7/h7_mac/CM7/Application/app_main.c) 在 V3 模式编译 V3 Core、Camera、USB 和 Storage 服务。
- [原 camera_task.c 入口槽](D:/32/STM32H7/h7_mac/CM7/Application/Tasks/camera_task.c) 在 V3 模式编译 `service_tasks.c`，提供 Camera、Usb、Storage 和 Control 四个原有任务创建接口。
- 其余 10 个 Legacy 专用编译单元在 V3 模式没有业务定义或 Initcall。保留在工程树中的旧文件名只是兼容原工程结构。
- 从 CM7 的预处理宏移除 `APP_V3_BUILD=1` 后，编译选择回到 Legacy。12 个条件编译文件的 Legacy 正文与基线逐个按字节对照一致。此轮完整 Rebuild 验证的是 V3 CM7 和原 CM4。
- V3 的生产实现仍位于 [CM7/ApplicationV3](D:/32/STM32H7/h7_mac/CM7/ApplicationV3)。本轮没有修改该目录中的原有实现，也没有修改驱动、消息 ABI、Replay、IRQ、链接脚本或任务配置。

前两次尝试仅改 CM7 文件组，Keil 日志显示原应排除的应用及另一核源文件仍被编译，导致重复链接符号或架构头文件报错。使用基线 `.uvoptx` 的第二次尝试仍可复现。两次原始日志均保留。最终保持原双核文件清单，只在 CM7 设置编译选择，使用用户原有 `.uvoptx` 完成双核 Rebuild。 [首次 CM7 原始错误](D:/32/STM32H7/h7_mac/output/ApplicationV3/KeilIntegration/20261120T142050Z-conflicts/h7_mac_CM7.rebuild.log) · [第二次 CM4 原始错误](D:/32/STM32H7/h7_mac/output/ApplicationV3/KeilIntegration/20261120T142050Z-final/h7_mac_CM4.rebuild.log)

## 构建、Map 与 ABI

| 配置 | Code | RO | RW | ZI |
| --- | ---: | ---: | ---: | ---: |
| 原 CM7，Phase 1 Legacy 对照 | 137690 B | 3114 B | 1340 B | 100932 B |
| Phase 1 独立 V3 对照 | 67542 B | 2870 B | 836 B | 95620 B |
| 原 CM7，当前 V3 | 67462 B | 2938 B | 836 B | 95652 B |
| 原 CM4 | 26302 B | 1438 B | 936 B | 3576 B |

最终 ARMCC5 Rebuild 的两个 Target 都退出 0，0 Error、0 Warning。CM7 Map 只有一组应用启动和 V3 任务入口。保留原 IPC 任务、真实 CameraCapture 校验器及 FreeRTOS 队列，旧 Camera 资格、Legacy Bridge Initcall 和 V2 Runtime 未进入镜像。CM4 Map 无 V3 符号，HEX 与 Phase 1 原 CM4 对照逐字节匹配。其 SHA256 为 `3030484c03d4983a192be39142dce7b6dfdb8e6c93a49e1f8285ee846540fb17`。 [双核验证](D:/32/STM32H7/h7_mac/output/ApplicationV3/KeilIntegration/20261120T142050Z-integrated/verification.json) · [CM7 Map](D:/32/STM32H7/h7_mac/output/ApplicationV3/KeilIntegration/20261120T142050Z-integrated/h7_mac_CM7/h7_mac_CM7.map) · [CM4 Map](D:/32/STM32H7/h7_mac/output/ApplicationV3/KeilIntegration/20261120T142050Z-integrated/h7_mac_CM4/h7_mac_CM4.map)

CM7 DTCM 占用 63400 B。Camera、USB、Storage、Control 和 IPC 的静态栈分别为 6144、8192、8192、3072、2048 B，优先级仍为 3、4、1、2、1。ARMCC5 O0/O2 的 V3 启动专项各 7/7，通过 AppMessage 52B、LifecycleMessage 56B 断言。运行期栈高水位、实板启动、出图和录像为 NOT_RUN。 [软件测试结果](D:/32/STM32H7/h7_mac/output/ApplicationV3/KeilIntegration/20261120T142050Z-conflicts/startup/results.json)

本次只完成原工程接入与验证，停在当前阶段等待人工审核。
