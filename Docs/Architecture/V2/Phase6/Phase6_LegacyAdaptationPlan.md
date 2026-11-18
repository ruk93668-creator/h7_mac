# Phase 6 Legacy 与实板适配门禁

已实现 `VStorageFS_Ops`，连接现有 StorageFS、MjpegAvi、JPEG 及 SD Block 公共接口。使用真实 StorageFS、FatFS、DiskIO、SD Block，对虚拟 FAT16 卷完成读写和恢复测试。以下是真实任务与硬件接线待审项，没有在本次独立实现中应用。

| 旧入口 | 最小适配 | 验收条件 |
| --- | --- | --- |
| CM7/Application/Tasks/storage_task.c，StorageTask_Run | 正式切换时同一个 Storage owner 轮询 VStorageEndpoint，迁移 BOOT、Attach、工作量与等待规则 | 不同时执行 Legacy App_Storage 与 VStorage，不改变任务栈和优先级 |
| CM7/Application/app_storage.c 和 app_frame_dispatcher.c，命令处理、Offer/ProcessReturns | 上层配置转为候选 Storage sink、路径、模式和限额。Camera 统一校验 G/S，再使用原 JPEG Retain，绑定真实 M。迁移录制分段、命名、容量检查、错误和状态兼容 | 旧接口不会绕开准入或开启另一持有；文件链路等价须专项审批 |
| CM7/Application/app_camera.c，当前 Qualification released/idle；CameraTask 原回合 | Camera owner 用同一 MRContext 审核资格与物理资源。在一个进度序中串行 Admit 和 Close，旧队列晚到 Offer 安全终结 | 无 USB/Storage 共用布尔状态。R 独立匹配原 G，验证 T 消费后才可退休。无法核验继续阻塞 |
| CM7/Media/Src/camera_mjpeg_fifo.c，RetainFrame/ReleaseLease | 迁移时用真实槽位/代次的只读权威证据，保留原两路 owner。Storage-only 路由的 Camera 内部持有独立考察 | 避免第二引用计数或地址推导 M，Raw 单物理 owner 约束保持 |
| CM7/Platform/storage_fs.c、diskio.c、sd_block.c、BSP SDMMC | 复用同步写入、复制输入、等待及显式恢复。真实 IO 取证覆盖原始 JPEG CPU 读及 SDMMC/IDMA 静止 | 记录真实 Abort 成败及 active/busy/fault，掉卡、部分写、文件 close/unmount 失败须保留实际结果 |

真实板需记录固件 SHA、板型、测试介质类型、FIFO Loan/Lease、G/R/H/M/S/T、每次 IDMA 使用/Abort 结果、准确的队列交付与消费。覆盖录像、抓拍、坏 JPEG、USB 与 Storage 独立退出、持续 FPS、卡拔出、卡满、写入超时、重启旧消息。若同一格式仍存在旧绕行入口或真实 ACK 消费无法核验，继续阻塞。两个总体缺口保持 NOT_PROVEN。
