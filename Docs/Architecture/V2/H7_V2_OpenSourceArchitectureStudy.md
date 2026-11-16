# Application V2 — 成熟开源架构源码研究

## 1. 研究方法、版本与边界

本轮实际读取官方仓库源码，而非只读框架介绍。访问时间：2026-11-16。Linux固定v6.12，GStreamer固定1.24.10，Zephyr固定v4.0.0；ROS 2 rolling先查询官方仓库commit，再用固定SHA读取：rcl `24eab4efb5043025bc5eac4ef408fa94867b283c`，rclcpp `917c1240535e18fe2201c20326eae26e1e477e5b`。这些是研究版本，不声称是最新稳定版。

下面行号按下载的原始文件计算，浏览器格式化后的行号可能不同。只记录结构、函数与机制，不拷贝框架实现进工程。SOURCE_CONFIRMED_EXTERNAL指外部源码事实；H7对应关系另标SOURCE_CONFIRMED；迁移选择均为UNRESOLVED（建议，待人工批准）。没有编译或运行任何外部项目、本项目测试或实板程序。

## 2. Linux Media Controller

### 官方源码与关键对象

- [include/media/media-entity.h，v6.12](https://github.com/torvalds/linux/blob/v6.12/include/media/media-entity.h)：media_pipeline（108）、media_link（176）、media_pad（236）、media_entity（349）、media_entity_operations。
- [drivers/media/mc/mc-entity.c，v6.12](https://github.com/torvalds/linux/blob/v6.12/drivers/media/mc/mc-entity.c)：media_pipeline_start（917）、__media_pipeline_start、media_pipeline_stop（955）、media_entity_setup_link（1357）、media_pipeline_populate和link_validate调用。

SOURCE_CONFIRMED_EXTERNAL：Entity包含pads/links/ops；Pad属于Entity并有方向；Link连接两个Pad。Pipeline start沿可用链路收集Pad，校验占用和link，再提交关联；失败撤销已提交部分。stop按start_count管理占用关系。外层以graph_mutex保护。它不是直接启动或停止传感器/DMA的入口。[源码依据](https://github.com/torvalds/linux/blob/v6.12/drivers/media/mc/mc-entity.c#L802)

适合H7：显式端点、能力交集、验证后提交、失败保持原图。H7自行选择const静态表和启动时冻结，不声称Linux本身使用同样静态模型。不移植动态对象、内核链表/锁、设备节点、动态link操作或图使用计数来替代帧引用。

SOURCE_CONFIRMED对应：`CM7/Application/app_frame_dispatcher.c::App_FrameDispatcherPublish`目前仅Storage服务；`app_uvc_bridge.c::App_UvcBridgeCameraProcess`是真正USB/SD分叉。`camera_capture.c`中的固定池/FIFO是互斥后端。目标Graph描述这些数据路由，但不替代它们实际执行或所有权。Graph Pipeline状态与Camera运行状态必须分离。

## 3. ROS 2 Managed Lifecycle

### 官方源码与关键对象

- [rcl_lifecycle/include/rcl_lifecycle/data_types.h，固定rcl SHA](https://github.com/ros2/rcl/blob/24eab4efb5043025bc5eac4ef408fa94867b283c/rcl_lifecycle/include/rcl_lifecycle/data_types.h)：rcl_lifecycle_state_t（39）、transition_t（53）、state_machine_t（114）；current_state、transition_map与通信接口分离。
- [rcl_lifecycle/src/default_state_machine.c](https://github.com/ros2/rcl/blob/24eab4efb5043025bc5eac4ef408fa94867b283c/rcl_lifecycle/src/default_state_machine.c)：_register_primary_states、_register_transition_states、_register_transitions、rcl_lifecycle_init_default_state_machine（658）。
- [rcl_lifecycle/src/rcl_lifecycle.c](https://github.com/ros2/rcl/blob/24eab4efb5043025bc5eac4ef408fa94867b283c/rcl_lifecycle/src/rcl_lifecycle.c)：_trigger_transition（375）、trigger_transition_by_id/label。
- [rclcpp_lifecycle/include/rclcpp_lifecycle/lifecycle_node.hpp](https://github.com/ros2/rclcpp/blob/917c1240535e18fe2201c20326eae26e1e477e5b/rclcpp_lifecycle/include/rclcpp_lifecycle/lifecycle_node.hpp)：LifecycleNode、configure/activate/deactivate/cleanup及trigger_transition。
- [rclcpp_lifecycle/src/lifecycle_node_interface_impl.cpp](https://github.com/ros2/rclcpp/blob/917c1240535e18fe2201c20326eae26e1e477e5b/rclcpp_lifecycle/src/lifecycle_node_interface_impl.cpp)：change_state（392）、execute_callback（508）。

SOURCE_CONFIRMED_EXTERNAL：状态图区分稳定态与转换态；业务回调结果选择success/failure/error转换，ERROR再执行错误回调。change_state尾部返回RCL_RET_OK可表示转换机制已完成，不保证业务回调成功。底层_trigger_transition先改变current_state，再发布通知，通知失败不会自动回滚真实状态。[运行机制源码](https://github.com/ros2/rclcpp/blob/917c1240535e18fe2201c20326eae26e1e477e5b/rclcpp_lifecycle/src/lifecycle_node_interface_impl.cpp#L440)

适合H7：每模块统一转换词汇、完成条件和错误出口；业务动作已发生而回复失败时保留事实及冻结结果。异步owner任务替代同步回调链，不引入ROS通信、executor、动态字符串/allocator、C++对象树或额外监督任务。

SOURCE_CONFIRMED对应：`app_camera.c::App_CameraFinishRound`、`app_usb.c::App_USBFinishRound`已有独立收尾；`app_camera_qualification.c::Qual_Decide/Qual_Submit`已经分开业务决策和可靠回复。V2复用这个分离原则，不能另建统一running/stopping权威，也不能把ROS返回值机械映射为业务成功。

## 4. V4L2 / videobuf2

### 官方源码与关键对象

- [include/media/videobuf2-core.h，v6.12](https://github.com/torvalds/linux/blob/v6.12/include/media/videobuf2-core.h)：vb2_buffer_state（226）、vb2_buffer（252）、vb2_ops（434）、vb2_queue（611）、vb2_mem_ops。
- [drivers/media/common/videobuf2/videobuf2-core.c](https://github.com/torvalds/linux/blob/v6.12/drivers/media/common/videobuf2/videobuf2-core.c)：vb2_core_qbuf（1831）、__enqueue_in_driver（1537）、vb2_buffer_done（1182）、vb2_core_dqbuf（2131）、vb2_start_streaming（1775）、vb2_core_streamon（2306）、vb2_core_streamoff（2365）、__vb2_queue_cancel（2195）。

SOURCE_CONFIRMED_EXTERNAL：QBUF提交队列，驱动接收后ACTIVE；buffer_done把驱动完成交还框架，再由DQBUF交给使用者。队列状态与内存操作分开。启动失败和停止要求驱动归还所持buffer；框架检查owned_by_drv_count。本版本还存在警告后的防御性回收路径，H7不能照搬成超时强制释放仍被DMA/消费者访问的帧。[停止实现](https://github.com/torvalds/linux/blob/v6.12/drivers/media/common/videobuf2/videobuf2-core.c#L2195)

适合H7：排队、驱动持有、完成、使用者持有分别表达；后端Ops区分准备/开始/停止与实际完成；启动失败也要清点已取得的资源。不移植MMAP/USERPTR/DMABUF、用户空间接口、动态内存模型、Linux锁/线程和自动强制回收。

SOURCE_CONFIRMED对应：raw的FREE/DMA/READY/IN_USE；Storage的RESERVING/QUEUED/WRITING/RETURN_PENDING/RETURNING/FREE；FIFO的块引用和独立Loan。`App_StorageProcessReturns`执行真实ReleaseLease才置FREE，正是不能把“申请归还”当完成的边界。V2不在这些后端之上再维护owned引用计数。

## 5. GStreamer Buffer / Memory / Pool

### 官方源码与关键对象

- [gstbuffer.h](https://github.com/GStreamer/gstreamer/blob/1.24.10/subprojects/gstreamer/gst/gstbuffer.h)：GstBuffer（283），时间戳、pool和MiniObject。
- [gstbuffer.c](https://github.com/GStreamer/gstreamer/blob/1.24.10/subprojects/gstreamer/gst/gstbuffer.c)：_gst_buffer_dispose（777）、gst_buffer_new（860）、gst_buffer_insert_memory（1140）。
- [gstmemory.h](https://github.com/GStreamer/gstreamer/blob/1.24.10/subprojects/gstreamer/gst/gstmemory.h)：GstMemory（161），allocator/parent/maxsize/align/offset/size；ref/unref转MiniObject。
- [gstminiobject.c](https://github.com/GStreamer/gstreamer/blob/1.24.10/subprojects/gstreamer/gst/gstminiobject.c)：gst_mini_object_ref（464）、unref（648）。
- [gstbufferpool.c](https://github.com/GStreamer/gstreamer/blob/1.24.10/subprojects/gstreamer/gst/gstbufferpool.c)：GstBufferPoolPrivate（93）、set_active（507）、default_acquire_buffer、default_release_buffer（1315）、dec_outstanding（1197）。

SOURCE_CONFIRMED_EXTERNAL：Buffer描述媒体数据，可关联多个Memory块；内存范围和格式/时间不混为一个指针。最后unref进入dispose，池buffer交回pool复用。停池先flushing，outstanding未归零时延后最终stop；active=false并不表示全部buffer已回收。[池停用源码](https://github.com/GStreamer/gstreamer/blob/1.24.10/subprojects/gstreamer/gst/gstbufferpool.c#L537)

适合H7：不可变FrameView与物理后端分离，JPEG两span不等于双plane，关闭入口与等待归还分开。现有FIFO引用算法继续权威；不移植GObject、MiniObject原子引用、动态buffer/allocator、阻塞poll和另一套Pool计数。可复用的是契约，不是把GStreamer refcount再包在现有引用上。

SOURCE_CONFIRMED对应：`Common/Inc/camera_config.h::CameraFrame_t`和`Common/Inc/video_frame_view.h`描述现有数据；`CameraMjpegFifo_ReleaseLoan`最后owner离开才释放块。V2关联账本只能记录身份与阶段，不决定物理回收。

## 6. Zephyr Video Subsystem

### 官方源码与关键对象

- [include/zephyr/drivers/video.h，v4.0.0](https://github.com/zephyrproject-rtos/zephyr/blob/v4.0.0/include/zephyr/drivers/video.h)：video_format/video_caps、video_buffer（123）、video_endpoint_id（210）、video_driver_api（354）；video_enqueue（521）、dequeue（548）、flush（574）、stream_start（597）、stream_stop（617）。
- [drivers/video/video_common.c](https://github.com/zephyrproject-rtos/zephyr/blob/v4.0.0/drivers/video/video_common.c)：video_buffer_alloc（63）、video_buffer_release（68）。
- [drivers/video/video_stm32_dcmi.c](https://github.com/zephyrproject-rtos/zephyr/blob/v4.0.0/drivers/video/video_stm32_dcmi.c)：video_stm32_dcmi_stream_start（243）、stream_stop（269）、enqueue（291）、dequeue（314）；fifo_in/fifo_out和video_driver_api适配。

SOURCE_CONFIRMED_EXTERNAL：Video Device通过Ops提供格式/能力/流控制，Endpoint标识输入输出，Buffer区分capacity与bytesused。STM32适配器用入/出FIFO交接buffer。头文件stream_stop包装调用驱动stop后再flush，但本版本没有传播flush返回值；不能据此把stop返回0当作H7退出证明。[包装函数](https://github.com/zephyrproject-rtos/zephyr/blob/v4.0.0/include/zephyr/drivers/video.h#L617)

适合H7：轻量C Ops、显式端点/格式、enqueue与dequeue契约，比完整Linux对象模型更适合MCU。保留FreeRTOS，不导入Zephyr device model、devicetree、k_fifo/k_heap或其STM32驱动；所读驱动先释放其内部堆buffer再调用HAL_DCMI_Stop的顺序，不作为本项目安全停止规范，也不据此对另一平台作运行故障结论。

SOURCE_CONFIRMED对应：DeviceRegistry有OV5640基础Ops，Media公开接口可以作为V2 Resource Port的后端；它不是要求把USB/SD强行包装成同一种Device。Zephyr Endpoint、Graph Pad、Camera Consumer是三种身份，不能复用同一个enum冒充关联。该接口本身也不能证明多消费者Grant→Loan，需要H7显式适配。

## 7. 实际研究文件指纹

SHA256用于复核此次读取的原始内容；没有把下载源码加入仓库。未在本工程编译这些文件。

| 官方相对路径/版本 | SHA256 |
|---|---|
| linux v6.12 include/media/media-entity.h | 9bcd44b74a0a3b5e7ad5cd1e78e2d5479c07130878c3fae35e2b816f6cc859bc |
| linux v6.12 drivers/media/mc/mc-entity.c | 9dedca52141c3a6ac2977761da4d5f2679ca000d64d860d0e03139031926bebf |
| linux v6.12 include/media/videobuf2-core.h | e49579466d9e6f4bd0f7b6f0425d7cccb488b5f630a7d0d3d9cb2d884a596827 |
| linux v6.12 drivers/media/common/videobuf2/videobuf2-core.c | f2a6802d0fce3cc2c318111dd51d8657322376ddf6f592c9fae756a8cdd28b0c |
| rcl固定SHA rcl_lifecycle/include/rcl_lifecycle/data_types.h | e71a47255392afc7ea9b33c6de3ca8d64ca35c9986325d091b037ab6121fe1b4 |
| rcl固定SHA rcl_lifecycle/src/rcl_lifecycle.c | 6f9dc3ce6749a17633e33e63c1f1a878ea39fb62de024b8bfc6019ec01919c91 |
| rcl固定SHA rcl_lifecycle/src/default_state_machine.c | 2f910cd90256f9b3e3e4cb05bb87d27993be097c98466e86e3c7844b726247fd |
| rclcpp固定SHA rclcpp_lifecycle/include/rclcpp_lifecycle/lifecycle_node.hpp | 2d7772d7405ce370edae4e815bc6e1bced3e423a92871c9c7eb31a36e963ce01 |
| rclcpp固定SHA rclcpp_lifecycle/src/lifecycle_node_interface_impl.cpp | 18174dfe2c57b83b5414647aacea94b654acd4efdd41c4ef9625f16e9e82251e |
| gstreamer 1.24.10 gstbuffer.h | 4a97572529146bf89e1350ba3f4a52896cffca58a28edff8c49990c909f2eb0a |
| gstreamer 1.24.10 gstbuffer.c | 3a4445940fb002a64dacd20918a9c87a3182272f99e74af5551e4f1deb78f7f1 |
| gstreamer 1.24.10 gstmemory.h | 28ae0469f8295f3e2888f27527b27a0f690a6da13b1d25966b786a3d1b0276d0 |
| gstreamer 1.24.10 gstminiobject.c | e5f7b166ae5e80642240f21b19c41c0bf485585ac3c42f19c90489bebede9a3b |
| gstreamer 1.24.10 gstbufferpool.c | 29a2b752f454d4774ca3f22319c6e65bd5af1a641a51b1f064e090f0c2d4c750 |
| zephyr v4.0.0 include/zephyr/drivers/video.h | 644aaf4ed2c2a80eba94f088e673057d5950abacd80aca2bd5da733e69f326a8 |
| zephyr v4.0.0 drivers/video/video_common.c | 6fafef6c013a57f693d8d09b6013b7d2a26958eccc33a0fa416c0aef6a4c8c64 |
| zephyr v4.0.0 drivers/video/video_stm32_dcmi.c | c25b6ab8d4594223ecdafdd4cb468f37db3e09089968e8135a5d761e329e41fe |

## 8. 研究之后的设计约束

不新造Graph分配器、通用状态总控、平行Replay或第二引用计数。H7特有的实时任务所有者、旧ABI、外部SDRAM、SD-only、USB双端ACK和Grant绑定需要本地适配；不能拿框架内部的一次“stop成功”替代全部事实。可移植思想已经落入ADR和ModuleContract；精确布局、内存预算、Resource proof与Supervisor计划接口继续UNRESOLVED，不因参考了成熟项目而自动获批。
