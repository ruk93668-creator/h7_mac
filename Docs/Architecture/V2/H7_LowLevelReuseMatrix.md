# V2 底层复用矩阵

基线 `8a03596`，只读审计；所有分类是迁移建议，不是本轮代码修改授权。

KEEP＝保留实现与公开行为；ADAPT＝保留算法、增加/调整明确的上层端口；REPLACE＝V2承接后替换旧应用实现；REMOVE＝承接验收后从活动源码/工程删除；UNRESOLVED＝需独立设计或硬件依据。本表主分类互斥，附加事项不改变当前实现。

## 1. 十二项逐项决策

| 项目 | 主决策 | 真实源码和入口 | 依据、影响与后续门禁 |
|---|---|---|---|
| BSP（Camera/GPIO/SDRAM） | KEEP | `CM7/BSP/bsp_camera.c::BSP_Camera_EarlyInit/BSP_Camera_Init`；`CM7/BSP/bsp_sdram.c` | 板级时钟、GPIO、存储窗口留在BSP。当前main是软件SCCB PB8 SDA/PB9 SCL，不含V1 CAM_DVDD_EN/I2C1；V1分支不是本基线，板型选择UNRESOLVED，另审不静默移植 |
| Device | KEEP | `CM7/Device/Inc/device.h`、`CM7/Device/Src/device.c::Device_RegisterAll/InitAll/Start/Stop`、`ov5640_device.c::DEVICE_EXPORT/Ov5640_ValidateConfig` | 静态导出目前OV5640；生命周期仅init/start/stop，无deinit/unregister。V2 Cleanup不能虚构Device_DeInit或注册USB/SD |
| Camera Capture | ADAPT | `CM7/Media/Src/camera_capture.c::CameraCapture_Init/Process/Stop/IsStopped/GetStopFacts` | 保留采集和停止算法、R2事实序号；仅设计V2端口与未来只读所有权查询。实际新增查询需独立审核，不修改FE策略 |
| DCMI/DMA | KEEP | `CM7/BSP/bsp_dcmi.c`；`CameraCapture_Process/Stop` 的调用边界 | IRQ、DMA target、FIFO阈值、错误捕获和保护性停止不由应用重写。停止事实必须区分硬件、DRAINING、完全停止 |
| CameraStream | ADAPT | `camera_capture.c::CameraStream_Init`、`CameraCapture_StreamGetFrame/StreamReleaseFrame`及内部buffer状态 | 固定池单个IN_USE、DMA目标排除。统一Resource视图，不新增同名物理池；并行多消费者能力不能从JPEG推断 |
| CameraMjpegFifo | ADAPT | `CM7/Media/Src/camera_mjpeg_fifo.c::GetFrame/RetainFrame/ReleaseFrame/ReleaseLease/HasLoan`（均为CameraMjpegFifo_前缀） | 保留块引用与Loan算法。后续只读身份查询/通用消费者扩展另审；HasLoan不是分消费者退出证明 |
| USB Device/UVC | KEEP | `CM7/Platform/usb_device.c`、`CM7/Service/usb_mode.c`、`usb_uvc.c::USB_UVC_Process/CancelFrame/GetStreamStatus`、`usb_uvc_desc.c::USB_UVC_ValidateFSConfig` | Core/EP0/Probe/Commit/Alt/Token/Abort不变；V2 USB模块替代旧业务层，不复制这些状态。Reset/Disconnect/Suspend/Resume不隐式释放资格 |
| SDMMC | KEEP | `CM7/Platform/sd_block.c::SD_Block_Claim/Release/Read/Write/Recover`；`CM7/BSP/bsp_sdmmc.c`、`bsp_sdmmc_memory.c` | 现有block owner、完成等待和恢复保留；32KiB AXI/8KiB复制/1ms参数不变。Resource只管媒体资源，不加另一SD owner |
| FatFS/StorageFS/JPEG/AVI | KEEP | `CM7/Platform/storage_fs.c`、`diskio.c`；`CM7/Service/jpeg_frame.c`、`mjpeg_avi.c` | 文件凭据/卷会话/部分写入/Sync/Finish错误保留，Storage owner独占。格式化、MSC、Volume Manager不在本阶段 |
| FreeRTOS | KEEP | `CM7/Application/Tasks/*_task.c`、`CM7/Core/Src/main.c`、工程的FreeRTOS源与配置 | 复用既有内核和五角色调度约束。旧任务源码最终替换，不等于替换RTOS。不得擅改优先级、栈、等待周期、IRQ门限或BASEPRI |
| HSEM/IPC | KEEP | `CM7/Core/Src/main.c::main`、`CM7/Platform/ipc_platform.c`、`CM7/Service/ipc_service.c`、`Common/Inc/ipc_application.h` | HSEM唤醒CM4，IPC共享区/屏障/owner/心跳协议保持；CM7应用状态适配需ADAPT，CM4/OLED延时保持字节不变 |
| Framework Event/Timer/Initcall | ADAPT | `CM7/Framework/framework_init.c::Framework_InitRuntime`、`framework_event.c`、`framework_timer.c`、`Common/Src/runtime_event.c` | 保留框架；V2承接应用导出、BOOT与运行owner绑定。Framework_Event无现成并发锁；Timer回调在Process调用上下文，不是独立RTOS Timer；需明确单owner而非直接用于多任务可靠传输 |

没有底层组件因 Clean Restart 被直接判为 REMOVE。REPLACE/REMOVE 主要针对 Legacy Application，逐文件退休见MigrationPlan。对现阶段无法证明的资源关联标为UNRESOLVED，不能用重写驱动代替证明。

## 2. 数据格式及物理资源复用边界

`Common/Inc/camera_config.h` 定义Y8/RGB565/YUV422/JPEG。`Ov5640_ValidateConfig` 当前非JPEG只接受160×120；JPEG接受160×120、320×240、640×480。`CameraCapture_ValidateStreamConfig` 固定帧还受容量、4字节整除及0xffff DMA words限制；三者取交集。UVC只接受YUV422/JPEG；USB FS每包1023、2字节头，有 `ceil(frame_bytes/1021) <= floor(1000/fps)` 的配置限制；不是保证实测FPS。StorageOffer只接受JPEG且≤70000字节。

MJPEG表示连续JPEG传输/AVI封装，不新增sensor枚举；JPEG两span是一平面的两段地址，不是双plane。固定池7×614400＝4300800字节，JPEG区512KiB+两个8KiB drop块＝540672字节；均为源码常量计算，不是本轮Map测量。原有缓冲配置、MPU、内存布局不变。

Resource统一FrameView和不透明后端身份，不能要求所有后端支持相同Retain次数。当前RGB565/Y8可保留Camera采集能力；不能假装UVC/Storage已支持。固定帧的多消费者若需新算法是独立范围，不能为了统一接口丢弃原支持格式，也不能把JPEG lease直接套给raw。

## 3. 驱动和Application分界

Application模块→Device/Media/Service公开端口→Platform/BSP→HAL。Camera的Device生命周期与Media停止是两个完成事实；USBMode running与UVC frame_owned不同；Storage文件Close与卷Unmount和帧Release不同。

本轮不执行链接、Map、硬件或性能测量。V1用户既往通过记录只说明相应历史固件/板型，不证明当前研究分支的软件SCCB固件适配V1。最终选择的硬件分支、固件SHA和板型必须在编码前冻结。
