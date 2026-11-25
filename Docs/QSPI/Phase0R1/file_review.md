# 旧主线至 V3 冻结主线逐文件差异审查

输入提交与每个旧/新文件 SHA-256 见 source_diff_audit.json。完整原始补丁见 old_to_v3_production.patch。本表覆盖全部77条生产路径变更（含重命名），未列文件由 Git 差异证明未改变。本次审核不等同运行验证。

|Git状态|旧路径 → 新路径|职责变化与QSPI适配影响|
|---|---|---|
|D|`CM7/Application/Tasks/camera_task.c`|旧Application实现删除；不能在新共存工程中恢复登记或沿用其任务入口。职责由V3 Core/Services/Tasks和Runtime承担|
|D|`CM7/Application/Tasks/camera_task.h`|旧Application实现删除；不能在新共存工程中恢复登记或沿用其任务入口。职责由V3 Core/Services/Tasks和Runtime承担|
|D|`CM7/Application/Tasks/control_task.c`|旧Application实现删除；不能在新共存工程中恢复登记或沿用其任务入口。职责由V3 Core/Services/Tasks和Runtime承担|
|D|`CM7/Application/Tasks/control_task.h`|旧Application实现删除；不能在新共存工程中恢复登记或沿用其任务入口。职责由V3 Core/Services/Tasks和Runtime承担|
|D|`CM7/Application/Tasks/storage_task.c`|旧Application实现删除；不能在新共存工程中恢复登记或沿用其任务入口。职责由V3 Core/Services/Tasks和Runtime承担|
|D|`CM7/Application/Tasks/storage_task.h`|旧Application实现删除；不能在新共存工程中恢复登记或沿用其任务入口。职责由V3 Core/Services/Tasks和Runtime承担|
|D|`CM7/Application/Tasks/usb_task.c`|旧Application实现删除；不能在新共存工程中恢复登记或沿用其任务入口。职责由V3 Core/Services/Tasks和Runtime承担|
|D|`CM7/Application/Tasks/usb_task.h`|旧Application实现删除；不能在新共存工程中恢复登记或沿用其任务入口。职责由V3 Core/Services/Tasks和Runtime承担|
|D|`CM7/Application/app_camera.c`|旧Application实现删除；不能在新共存工程中恢复登记或沿用其任务入口。职责由V3 Core/Services/Tasks和Runtime承担|
|D|`CM7/Application/app_camera.h`|旧Application实现删除；不能在新共存工程中恢复登记或沿用其任务入口。职责由V3 Core/Services/Tasks和Runtime承担|
|D|`CM7/Application/app_camera_lifecycle.c`|旧Application实现删除；不能在新共存工程中恢复登记或沿用其任务入口。职责由V3 Core/Services/Tasks和Runtime承担|
|D|`CM7/Application/app_camera_lifecycle.h`|旧Application实现删除；不能在新共存工程中恢复登记或沿用其任务入口。职责由V3 Core/Services/Tasks和Runtime承担|
|D|`CM7/Application/app_camera_lifecycle_replay.c`|旧Application实现删除；不能在新共存工程中恢复登记或沿用其任务入口。职责由V3 Core/Services/Tasks和Runtime承担|
|D|`CM7/Application/app_camera_lifecycle_replay.h`|旧Application实现删除；不能在新共存工程中恢复登记或沿用其任务入口。职责由V3 Core/Services/Tasks和Runtime承担|
|D|`CM7/Application/app_camera_qualification.c`|旧Application实现删除；不能在新共存工程中恢复登记或沿用其任务入口。职责由V3 Core/Services/Tasks和Runtime承担|
|D|`CM7/Application/app_camera_qualification.h`|旧Application实现删除；不能在新共存工程中恢复登记或沿用其任务入口。职责由V3 Core/Services/Tasks和Runtime承担|
|D|`CM7/Application/app_camera_qualification_internal.h`|旧Application实现删除；不能在新共存工程中恢复登记或沿用其任务入口。职责由V3 Core/Services/Tasks和Runtime承担|
|D|`CM7/Application/app_frame_dispatcher.c`|旧Application实现删除；不能在新共存工程中恢复登记或沿用其任务入口。职责由V3 Core/Services/Tasks和Runtime承担|
|D|`CM7/Application/app_frame_dispatcher.h`|旧Application实现删除；不能在新共存工程中恢复登记或沿用其任务入口。职责由V3 Core/Services/Tasks和Runtime承担|
|D|`CM7/Application/app_main.c`|旧Application实现删除；不能在新共存工程中恢复登记或沿用其任务入口。职责由V3 Core/Services/Tasks和Runtime承担|
|D|`CM7/Application/app_messages.c`|旧Application实现删除；不能在新共存工程中恢复登记或沿用其任务入口。职责由V3 Core/Services/Tasks和Runtime承担|
|D|`CM7/Application/app_storage.c`|旧Application实现删除；不能在新共存工程中恢复登记或沿用其任务入口。职责由V3 Core/Services/Tasks和Runtime承担|
|D|`CM7/Application/app_storage.h`|旧Application实现删除；不能在新共存工程中恢复登记或沿用其任务入口。职责由V3 Core/Services/Tasks和Runtime承担|
|D|`CM7/Application/app_usb.c`|旧Application实现删除；不能在新共存工程中恢复登记或沿用其任务入口。职责由V3 Core/Services/Tasks和Runtime承担|
|D|`CM7/Application/app_usb.h`|旧Application实现删除；不能在新共存工程中恢复登记或沿用其任务入口。职责由V3 Core/Services/Tasks和Runtime承担|
|D|`CM7/Application/app_uvc_bridge.c`|旧Application实现删除；不能在新共存工程中恢复登记或沿用其任务入口。职责由V3 Core/Services/Tasks和Runtime承担|
|D|`CM7/Application/app_uvc_bridge.h`|旧Application实现删除；不能在新共存工程中恢复登记或沿用其任务入口。职责由V3 Core/Services/Tasks和Runtime承担|
|D|`CM7/Application/video_config.h`|旧Application实现删除；不能在新共存工程中恢复登记或沿用其任务入口。职责由V3 Core/Services/Tasks和Runtime承担|
|A|`CM7/ApplicationV3/Core/app_context.h`|只读视频/存储配置上下文，QSPI不增加字段|
|R086|`CM7/Application/app_events.c → CM7/ApplicationV3/Core/app_events.c`|五任务所有权及一次BOOT屏障；测试线程不能登记为业务owner；保留迁移前后身份/注释差异证据|
|R078|`CM7/Application/app_events.h → CM7/ApplicationV3/Core/app_events.h`|五任务所有权及一次BOOT屏障；测试线程不能登记为业务owner；保留迁移前后身份/注释差异证据|
|A|`CM7/ApplicationV3/Core/app_main.c`|APPLICATION级准备Context和Record，未执行QSPI初始化|
|R060|`CM7/Application/app_main.h → CM7/ApplicationV3/Core/app_main.h`|APPLICATION级准备Context和Record，未执行QSPI初始化；保留迁移前后身份/注释差异证据|
|A|`CM7/ApplicationV3/Core/app_messages.c`|静态通道分配、方向/发送者校验和帧归还消息路由|
|R053|`CM7/Application/app_messages.h → CM7/ApplicationV3/Core/app_messages.h`|静态通道分配、方向/发送者校验和帧归还消息路由；保留迁移前后身份/注释差异证据|
|A|`CM7/ApplicationV3/Core/app_services.c`|静态服务任务清单、Record发布及启动结果汇总；启动就绪不代表业务ACTIVE|
|A|`CM7/ApplicationV3/Core/app_services.h`|静态服务任务清单、Record发布及启动结果汇总；启动就绪不代表业务ACTIVE|
|R100|`CM7/Application/app_status.c → CM7/ApplicationV3/Core/app_status.c`|现有状态快照出口延续，不能承载新QSPI字段而改变80B ABI；保留迁移前后身份/注释差异证据|
|R094|`CM7/Application/app_status.h → CM7/ApplicationV3/Core/app_status.h`|现有状态快照出口延续，不能承载新QSPI字段而改变80B ABI；保留迁移前后身份/注释差异证据|
|R072|`CM7/Application/app_tasks.c → CM7/ApplicationV3/Core/app_tasks.c`|创建三个服务+Control+IPC并核对准备数量，独立测试不能扩充计数；保留迁移前后身份/注释差异证据|
|R072|`CM7/Application/app_tasks.h → CM7/ApplicationV3/Core/app_tasks.h`|创建三个服务+Control+IPC并核对准备数量，独立测试不能扩充计数；保留迁移前后身份/注释差异证据|
|R060|`CM7/Application/video_config.c → CM7/ApplicationV3/Core/video_config.c`|Raw/JPEG产品配方、SD分区3及录像上限；不是BOOT自动启动开关；保留迁移前后身份/注释差异证据|
|A|`CM7/ApplicationV3/Core/video_config.h`|Raw/JPEG产品配方、SD分区3及录像上限；不是BOOT自动启动开关|
|A|`CM7/ApplicationV3/Runtime/queue_pool.c`|固定槽、代次、订阅与在途引用共同保护队列生命周期|
|A|`CM7/ApplicationV3/Runtime/queue_pool.h`|固定槽、代次、订阅与在途引用共同保护队列生命周期|
|A|`CM7/ApplicationV3/Runtime/service_event_loop.c`|所属任务执行、单步最多4回调、100ms漏通知回查|
|A|`CM7/ApplicationV3/Runtime/service_event_loop.h`|所属任务执行、单步最多4回调、100ms漏通知回查|
|A|`CM7/ApplicationV3/Runtime/service_events.c`|Control分发实际硬件终结和保留首错；回调不得阻塞|
|A|`CM7/ApplicationV3/Runtime/service_events.h`|Control分发实际硬件终结和保留首错；回调不得阻塞|
|A|`CM7/ApplicationV3/Runtime/service_mailbox.c`|16B命令、深度4、requester身份及结果背压；无QSPI邮箱|
|A|`CM7/ApplicationV3/Runtime/service_mailbox.h`|16B命令、深度4、requester身份及结果背压；无QSPI邮箱|
|A|`CM7/ApplicationV3/Runtime/service_record.c`|静态实例发布和只读观察，QSPI不作为第四个服务发布|
|A|`CM7/ApplicationV3/Runtime/service_record.h`|静态实例发布和只读观察，QSPI不作为第四个服务发布|
|A|`CM7/ApplicationV3/Services/Camera/camera_service.c`|Camera任务独占采集/消费者Lease及帧分派；延迟归还会影响可用缓冲|
|A|`CM7/ApplicationV3/Services/Camera/camera_service.h`|Camera任务独占采集/消费者Lease及帧分派；延迟归还会影响可用缓冲|
|A|`CM7/ApplicationV3/Services/Storage/storage_service.c`|Storage任务独占卷、AVI和帧归还，QSPI不借用其句柄或帧|
|A|`CM7/ApplicationV3/Services/Storage/storage_service.h`|Storage任务独占卷、AVI和帧归还，QSPI不借用其句柄或帧|
|A|`CM7/ApplicationV3/Services/USB/usb_service.c`|USB任务独占UVC状态与Reader Proof，未取得退出证据不得归还|
|A|`CM7/ApplicationV3/Services/USB/usb_service.h`|USB任务独占UVC状态与Reader Proof，未取得退出证据不得归还|
|R095|`CM7/Application/Tasks/ipc_task.c → CM7/ApplicationV3/Tasks/ipc_task.c`|既有Heartbeat和80B应用快照发布，保持真实任务身份；保留迁移前后身份/注释差异证据|
|R100|`CM7/Application/Tasks/ipc_task.h → CM7/ApplicationV3/Tasks/ipc_task.h`|既有Heartbeat和80B应用快照发布，保持真实任务身份；保留迁移前后身份/注释差异证据|
|A|`CM7/ApplicationV3/Tasks/service_tasks.c`|公共服务主循环、Control启动结果与Supervisor；同步HAL不能置于其回调|
|A|`CM7/ApplicationV3/Tasks/service_tasks.h`|公共服务主循环、Control启动结果与Supervisor；同步HAL不能置于其回调|
|A|`CM7/ApplicationV3/Tasks/supervisor.h`|Control专用Set/Query/Read/Recover及组件依赖，QSPI不增加PIPE位|
|M|`CM7/BSP/bsp_camera.c`|软SCCB改硬件I2C1、PB9/8、PA15 DVDD、PH13 PWDN及上电顺序；无PF6–10/PG6冲突，不能沿用旧Camera描述|
|M|`CM7/BSP/bsp_camera.h`|新增PowerEnable/PowerDisable声明，QSPI不调用相机电源接口|
|M|`CM7/Media/Inc/camera_capture.h`|新增Raw/JPEG通用消费者Lease接口和身份类型|
|M|`CM7/Media/Inc/camera_mjpeg_fifo.h`|JPEG消费者Lease扩展及FIFO占用查询|
|M|`CM7/Media/Src/camera_capture.c`|Raw代次/借用身份、单消费者租约、轮换失败锁存及停止排空；保护七缓冲资源|
|M|`CM7/Media/Src/camera_mjpeg_fifo.c`|多消费者owner位、通用Acquire/Release、legacy隔离和占用快照；物理块回收需最后owner退出|
|M|`CM7/Platform/usb_device.c`|新增短临界区一致链路快照，恢复PRIMASK原值|
|M|`CM7/Platform/usb_device.h`|新增链路状态值类型接口，QSPI不修改该API|
|M|`CM7/Service/mjpeg_avi.h`|索引从4096增至65536项，1MiB仍位于0xD0590000；QSPI不占其SDRAM|
|M|`CM7/Service/usb_uvc.h`|新增accepted/retired/退出原因及首错Reader Proof接口|
|M|`CM7/Service/usb_uvc_class.c`|UVC接收号、取消/EOF/关闭退出证明与首错，不能用QSPI Abort替代读者退休|
|M|`Common/Inc/camera_config.h`|新增USB/Ethernet/WiFi/Storage消费者身份枚举；已声明身份不等于其他产品已集成|
|M|`MDK-ARM/h7_mac.uvprojx`|V3源文件/分组登记与CM7 Core Include路径，保留Supervisor链接符号；CM4列出排除组但有效C/ASM集合不变|
