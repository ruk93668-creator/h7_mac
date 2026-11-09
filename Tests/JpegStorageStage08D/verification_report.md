# Stage08D软件交付报告

日期2026-11-09。基线41a4ba4405d1a9eae5c84f9c9764432f3d052d76；分支codex/stage08d-jpeg-storage；隔离工作树D:/32/STM32H7/h7_stage08d。原h7_mac的08C及用户设置未覆盖，不合并main。

## 实现和边界

一条JPEG管线，Camera端唯一Media取还门；原USB租约与SD副本并行。NONE支持SD-only，ETH/Wi-Fi/AI明确不支持。SD默认OFF，无自动挂载、录像或抓拍。非JPEG/TEST拒绝保存，不能生成伪JPG。

四个70000B副本槽，70016B步长；独立帧/命令队列各4项。真实不连续双段在原租借期间完整复制，队列携带槽位代号、存储会话和帧元数据；满池只丢SD帧，不覆盖WRITING或提前归还USB。采用静态Ops+Context副本服务及统一Storage事件分发/状态转换，未引入通用注册容器或SceneManager。

StorageTask静态2048 Words、优先级1、第五BOOT目标，唯一使用StorageFS。原四任务优先级和栈不变。JPG新建、分批写、sync/close、重开、逐字节/长度/CRC校验、卸载，全部成功才增加抓拍成功计数。录像AVI1.0包括RIFF/hdrl/movi/00dc/idx1、偶数填充、长度及平均帧率回填；有界切段、空间和总量限制，CREATE_NEW不覆盖文件。

恢复遵守真实08C契约：仅真实故障租约调用Recover；普通挂载态重试关闭/卸载；已READY解除应用故障。恢复不证明原文件完整。

## 软件验收

| 项目 | 实际结果 | 记录 |
|---|---|---|
| 75个08D场景，ARMCC O0/O2 | 150/150 PASS | results.json |
| Bridge兼容与交错 | 35/35 PASS，含原28项不改断言 | bridge_test_results.json |
| 原五缓冲三格式 | 90/90 PASS，原断言不改 | media_test_results.json |
| 真实FreeRTOS静态任务、BOOT和阻塞 | 6/6 PASS | task_results.json |
| AVI解析、索引、ffprobe及完整解码 | 11/11检查通过 | avi_validation.json |
| 现行14套IPC/OLED/USB/Camera/DCMI | 1899次执行通过 | retained_regressions.json |
| 原08C FatFS | 108/108 PASS | fatfs_regression.json |
| 原08B SD块设备 | 556/556 PASS | sd_block_regression.json |
| 范围、设置、ELF内存 | 21/21 PASS | audit_results.json |

编译器ARMCC5.06 update7 build960，Unicorn2.1.4。直接编译真实应用/AVI/JPEG/Bridge源码，文件介质和硬件端只在RAM模拟，未访问实卡。严格模拟非FAULT不能调用08C Recover，防止宽松桩掩盖错误调用。

覆盖：双段SOI跨界、70000B上限、超长、不完整JPEG、满池/命令满、重复/过期归还、队列会话和代号、复制中停止、停止排空、分段、总量、空间不足、无卡/掉卡、短写、同步/关闭/卸载错误、恢复重试、原帧归还后被覆盖、USB Abort失败及USB-only/SD-only/USB+SD交错。

Media千帧长序列最初因100M仿真指令预算耗尽而未返回，对长用例给足2B预算后全部正常返回，未改断言。首次两套IPC测试缺少第五任务桩；补齐静态接收端和严格5任务计数后原61项通过。初次CM4自动同步新增CM7组，已显式排除，未改CM4源码。

## 构建与内存

正式CM7和CM4完整重编均0 Error、0 Warning，源码前后指纹一致。CM7 Code134554B、RO3042B、RW1380B、ZI90660B；CM4 Code26302B、RO1438B、RW936B、ZI3576B。ZI含AXI区，不能全部当DTCM。DTCM执行分配至0x2000E648。

附加MJPEG USB、USB TEST、JPEG SD-only三种配置均0 Error/Warning；只是重编video_config并与已全量构建的同源对象重新链接，不宣称三个Target逐个全量重编。详见profile_builds.json。未修改Keil输出目录/名称、Define、Scatter和uvoptx，只新增编译组及CM4排除。

副本地址：D0384000、D0395180、D03A6300、D03B7480，结束D03C8600；AVI索引D03D0000..D03E0000。现有4MiB Non-cacheable/Shareable MPU窗口内，没扩大MPU。IDMA仍24000000/32768B，IPC仍38000000/320B，Storage栈2000B3E8/8192B。编译断言与真实ELF执行地址检查一致。

## 修改清单和审计

新增Application/app_storage.c/.h、app_frame_dispatcher.c/.h、Tasks/storage_task.c/.h；Service/jpeg_frame.c/.h、mjpeg_avi.c/.h。
调整app_tasks（第五任务）、Common/runtime_event.h（追加本地目标）、video_config（独立SD策略）、app_main（允许NONE）、control_task（NONE为SD准备Camera）、app_messages.h（追加帧元数据）、Bridge Camera侧（副本和SD-only取还），及Keil必要文件组。

CameraTask、UsbTask、IPC任务、App Events实现、App Messages队列实现、App Status、Media/FIFO、Device、BSP/SDMMC/IDMA、MPU/Scatter、HAL/LL、USB/UVC/描述符、FatFS/StorageFS/SD_Block和CM4生产源码均未修改。USB侧Process、SameLease、UsbReturn逐字审计不变。

历史测试只调整IPC任务夹具容量/Storage创建桩/严格5任务数及两处用例名称，不删除或降低断言。历史测试生成报告保持工作区原样、不纳入提交；当前证据另存08D。最终文件清单见scope_audit.json。

## 实板未执行和限制

本阶段没有烧录或操作真实卡；08C实板通过不代替08D。真实JPG电脑解码/CRC、AVI录制/封尾/解码播放、三种并行场景实际FPS/丢帧/水位/耗时/IPC/栈余量、新SDRAM地址以及持续5分钟负载均待用户验收。

软件AVI使用明确标记的合成JPEG，不能冒充真实视频。AVI平均播放FPS不是Camera/USB实测FPS。仅固定帧率AVI1.0，不保留逐帧时距、不补重复帧，无音频/OpenDML。实际JPEG上限70000B，不截断/自动扩容。文件名搜索最多128次、编号最多99999，不覆盖/删除旧文件。

NONE禁止主输出，但Camera仍为SD待命；不做无消费者自动关闭传感器。SD录像独立于USB断连，停止整个Camera前先结束存储。CRC/验证/复制会增加CPU和SDRAM负载，软件不能证明实测FPS不变。

空间预留是应用预算，FAT簇取整仍可能导致真实短写；掉卡/短写可能留下不能播放的末段。正常封尾前不保证AVI可播放，不实现掉电事务或失败文件修复。计时为毫秒，包含抢占和校验/回读等工作，0不表示零耗时。Mock不完整模拟NVIC抢占、物理总线和卡内缓存。

软件验收通过，硬件验收未完成。设计和实板步骤见同目录链接及Docs/Design；Commit/远端一致性在交付消息报告。
