# H7 Current-to-Target Mapping

## 1. 冻结基线（FACT）

2026-11-16获取origin main成功，本地main和FETCH_HEAD均f0ce45fd9ab7250b1e22260e087d606ee5ed9ecb。独立分支codex/architecture-media-graph-lifecycle由该main创建，研究树初始干净。C2.2A aa38870/fa9ce5d、C2.1.1 c6d2bed、BASEPRI 1c97d7c及R2组合f2da385均是main祖先；main有PR#6/PR#7的普通merge历史。这里确认提交历史完整包含，不把aa38870的旧生产树误称等于R2后的main树。

C2.2B工作分支起点b6541af，相对main是0/29（main独有/支线独有）。暂停时新增3份未完成Final Closure草稿；为响应分支备份要求，仅以WIP文档提交d815208推送C2.2B，没有补完矩阵或宣称Final Closure完成。研究分支没有导入该分支任何代码或文档，也不继承方案A字段预算。

main存在C2.2A Qualification/Replay工具，但CameraTask未调用Prepare/Receive/Process/Flush，USB/Storage没Consumer Client接入，released/idle NULL。main仍ControlTask旧Camera/USB协调，Camera BSP仍软件SCCB V0连线；V1 I2C1/CAM_DVDD_EN在fix支线，不能假设main具有V1验证结果。是否采用其他基线要另审，不在Phase0合并。

## 2. 四类概念不可互相代替

| 概念 | 官方指导 | main映射 | 本地建议，非已有代码 |
|---|---|---|---|
| Media Controller graph | 连接/端口/实体的拓扑模型 | sensor/DCMI/Media/Dispatcher/sinks在代码中隐式固定 | const图描述与格式兼容验证，图不持Frame |
| Managed Lifecycle | 稳定态与转换/失败区分 | App业务状态、Device状态、Media排空各自存在 | 通用生命周期观察/请求契约，复用App真源 |
| vb2 queue ownership | 驱动取得/完成/归还缓冲的边界 | CameraStream DMA/READY/IN_USE，JPEG块/loan | 资源适配保留各物理算法，明确stop/drain |
| GstBuffer/Pool | 描述、存储及池回收分离 | CameraFrame描述+静态SDRAM池、双span | 类型化只读资源句柄及有界owner合同 |

四个参考的官方来源分别链接于MediaGraph、ManagedLifecycle、MediaBuffer三文；本表是本项目推断，不宣称这些框架要求H7采用某个自创协议。

## 3. 当前职责分类

| 模块 | 分类/状态权威 | 实际边界 | 演进建议 |
|---|---|---|---|
| app_main/app_tasks | Application启动入口/任务创建记录 | Initcall及BOOT，不拥有帧 | 保留，不做万能管理器 |
| App_Camera | Camera业务唯一ctx | Device/Media启停和ACK/FAULT | 生命周期转换适配只读该ctx |
| CameraTask | RTOS执行上下文 | 消息、Media→Bridge→FinishRound、诊断 | 保持唯一实际Media归还执行 |
| App_USB/UsbTask | USB业务及调度分离 | Mode和显式UVC/Bridge推进 | 协议面不吞进通用Process |
| USB_Mode | 模式/枚举服务 | WAIT_CONFIGURED/RUNNING等 | 不与业务Active合并 |
| USB_Device/UVC | USB平台资源及Class服务 | EP0/端点/Token/Abort | 完成与Frame归还条件保留 |
| App_Storage/StorageTask | 录像/抓拍业务，Task独占FS | 文件结果、slot/generation，Camera归还 | 源引用与文件结束分别核验 |
| FrameDispatcher | Application帧路由/Media分发边界 | 生成meta并决定SD offer | 逐sink需求/格式/背压合同 |
| CameraCapture/FIFO | Media资源管理 | 真缓冲/loan、DMA排空 | 资源所有权唯一真源 |
| OV5640 Device | Device硬件抽象 | init/start/stop及基础配置 | 不接管消费者业务 |
| BSP Camera/DCMI | Board/驱动硬件实现 | GPIO/SCCB/XCLK/DCMI/DMA | Application不得绕层直调HAL |
| JPEG/AVI | 格式Service | 校验、容器、索引与封尾 | 不决定Camera资格 |
| StorageFS/FatFS/DiskIO | 文件服务/平台适配 | 卷/句柄/错误与LBA映射 | 不读取业务状态猜Frame生命周期 |
| SD_Block/BSP SDMMC | 块访问所有权/硬件传输 | FATFS/RAW claim，事件等待，IDMA | 已有claim不是新的VolumeManager，也不能把binary semaphore当互斥引用锁 |
| Framework | Initcall/Event/Timer/Registry机制 | 基础设施 | 复用，不另造总线/注册层 |

## 4. 重复职责审查

- Control启停和Camera自己的stopping不是同一职责：前者跨模块编排，后者真实停止；未来切业务自主生命周期时删旧控制权，而不是加graph总控与它们并行。
- Device.state、Capture.state、App running、USB_Mode.state、Storage.state是不同资源域，不应为“统一状态机”强合并。目标统一的是转换契约和状态来源，不是一个全局状态变量。
- AppMessage业务ACK、Bridge Return ACK、Lifecycle Reply分三个完成域；不能直接共用一个ACK成功条件。框架不会自动保证旧请求失效、DMA已完成或消费者已退出。
- Bridge held与Media usb_owned是协议在途/物理资源两种事实；若完全删除Bridge记录会失去队列ACK阶段。可收敛诊断缓存、避免业务从Debug取值，但不能用pool refcount替代运输握手。
- Storage slot和FIFO loan不是两套相同引用计数：slot承载文件在途访问，loan保证存储范围有效。未来关联须引用同一真实资源，不另维护匿名refcount。

## 5. 协议保留/简化候选

| 项目 | Phase0结论 |
|---|---|
| 原52B/56B ABI与Replay | 保持不改；成熟框架不提供可直接替代的嵌入式身份保证 |
| Frame Return/ACK | 需要保留运输与实际归还分离；后续可归入统一sink适配，不删语义 |
| C2.2B未合入协议适配/方案A/T字段 | 不属于main事实；暂停研究，不整体导入 |
| 新graph专用RPC/全局LifecycleManager/新事件总线 | 暂无必要，优先现有Task队列/Framework |
| Diagnostic Observer/App_Status | 诊断投影保留，不变成生命周期业务真源 |

## 6. 架构缺口与风险（事实与推断分离）

FACT：没有显式媒体图，Device只导出OV5640，消费者真实资格接入缺失，非JPEG与JPEG资源身份不同。UNKNOWN：main对应最新两核Map/实时栈峰值/实板V0或V1适配结果，本轮没有构建或硬件验证。

FACT：ControlTask_Process的STOP链只有USB/Camera，没有调用App_StorageStop；app_storage.c Process约549行在挂载且accepting、队列与offering均空、5000ms无输入时Storage_Fail(TIMEOUT)。据此需要专项验证“录像中旧全局停止是否错过正常封尾”的风险，不能把它归为graph可自动修复，也不能在未复现时宣布新的确定缺陷。

PROPOSAL：建立拓扑只读图及资源/生命周期适配可减少调用关系隐含性，但不会自动解决Grant→Lease和稳定关闭。它们仍需单独接口/证据设计；不能把ROS node或GstBuffer名字换上就宣布解决旧NOT_PROVEN。对原文件和Task调用顺序的影响必须在后续授权阶段逐项验证。

## 7. 精确源码定位索引（此main版本）

| 文件位置 | 函数 |
|---|---|
| `CM7/Application/app_tasks.c:18` | `App_TasksCreate` |
| `CM7/Application/app_tasks.c:100` | `App_TasksReleaseBoot` |
| `CM7/Application/Tasks/control_task.c:147` | `ControlTask_Process` |
| `CM7/Application/Tasks/control_task.c:247` | `ControlTask_Command` |
| `CM7/Application/Tasks/camera_task.c:112` | `CameraTask_Process` |
| `CM7/Application/Tasks/usb_task.c:118` | `UsbTask_Process` |
| `CM7/Application/Tasks/storage_task.c:31` | `StorageTask_Run` |
| `CM7/Application/app_camera.c:508` | `App_CameraStartResource` |
| `CM7/Application/app_camera.c:123` | `App_CameraAdvanceStop` |
| `CM7/Application/app_camera.c:365` | `App_CameraFinishRound` |
| `CM7/Application/app_camera.c:626` | `App_CameraQualificationAccess` |
| `CM7/Application/app_usb.c:46` | `App_USBHandleCommand` |
| `CM7/Application/app_usb.c:124` | `App_USBFinishRound` |
| `CM7/Application/app_uvc_bridge.c:137` | `App_UvcBridgeCameraProcess` |
| `CM7/Application/app_uvc_bridge.c:268` | `App_UvcBridgeUsbProcess` |
| `CM7/Application/app_uvc_bridge.c:456` | `Bridge_CameraRelease` |
| `CM7/Application/app_uvc_bridge.c:432` | `Bridge_CameraFlush` |
| `CM7/Application/app_storage.c:237` | `App_StorageOfferFrame` |
| `CM7/Application/app_storage.c:334` | `App_StorageProcessReturns` |
| `CM7/Application/app_storage.c:379` | `App_StorageProcess` |
| `CM7/Application/app_storage.c:638` | `Storage_OnStart` |
| `CM7/Application/app_storage.c:920` | `Storage_Finish` |
| `CM7/Application/app_frame_dispatcher.c:61` | `App_FrameDispatcherPublish` |
| `CM7/Device/Src/device.c:14` | `Device_RegisterAll` |
| `CM7/Device/Src/device.c:141` | `Device_Init` |
| `CM7/Device/Src/device.c:189` | `Device_Start` |
| `CM7/Device/Src/device.c:237` | `Device_Stop` |
| `CM7/Media/Src/camera_capture.c:1107` | `CameraStream_GetFrame` |
| `CM7/Media/Src/camera_capture.c:1184` | `CameraStream_ReleaseFrame` |
| `CM7/Media/Src/camera_capture.c:695` | `CameraCapture_Stop` |
| `CM7/Media/Src/camera_capture.c:1164` | `CameraCapture_StreamGetFrame` |
| `CM7/Media/Src/camera_mjpeg_fifo.c:325` | `CameraMjpegFifo_GetFrame` |
| `CM7/Media/Src/camera_mjpeg_fifo.c:377` | `CameraMjpegFifo_RetainFrame` |
| `CM7/Media/Src/camera_mjpeg_fifo.c:424` | `CameraMjpegFifo_ReleaseLease` |
| `CM7/Platform/storage_fs.c:333` | `StorageFS_Write` |
| `CM7/Platform/sd_block.c:41` | `SD_Block_Claim` |
| `CM7/Platform/sd_block.c:72` | `SD_Block_Release` |

附：CM7/Device/Inc/device.h Ops字段、Common/Inc/camera_config.h格式/Frame描述、CM7/Media/Inc/camera_mjpeg_fifo.h常量、CM7/Application/Tasks各文件栈/优先级宏也均在本基线读取；具体算法以以上函数为准，不借历史测试报告替代源码事实。
