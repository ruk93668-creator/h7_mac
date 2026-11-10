# 七缓冲与双路租借实板步骤

工程仍为D:/32/STM32H7/h7_stage08d/MDK-ARM/h7_mac.uvprojx，当前分支codex/stage08d-jpeg-storage。用户的Target/输出设置保持原样，不另建测试工程。必须Download同一AXF，不能只LOAD调试符号。IPC1秒阈值在Common，烧录本分支配套CM7/CM4并完整断电启动。

1. 保持用户320×240 JPEG、25FPS、40000B配置，确认APP_VIDEO_MJPEG_MODE=1；实际FPS独立测量。YUY2配置另行冷启动验证，缓冲7块但不开放新尺寸。
2. SD默认OFF。先不打开播放器、不触发存储，观察Camera采集、FIFO丢帧和IPC；再打开播放器观察USB实际FPS。复杂画面分别对照，不把目标25冒充实测。
3. Watch添加g_storage_debug、g_camera_task_debug、g_usb_task_debug、g_bridge_debug，以及两核各自的g_ipc_runtime_debug。所有绝对地址可能变化，重新加载符号，不复用旧截图地址。
4. 视频运行时request=2抓拍：预期state最终0、result=cleanup_result=0、saved_files/saved_frames各加1，last_size真实、last_crc=read_crc。pending_returns最终0、returned_frames加1、return_result=0；Camera与USB继续运行。max_copy_ms已移除，改为max_offer_ms，仅统计登记租借，不是写入时间。
5. request=1录像10秒，再request=3正常封尾。队列最终0、pending_returns最终0，saved_files增加，电脑验证JPG和AVI解码。SD原始帧按两段直接写入，经原32KiB AXI中转，不再有整帧副本。
6. SD最多占一张：queue_depth/queue_high_water不得超过1；WRITING和pending_returns仍算同一占用，确认归还前不接收第二张。USB独立持有原帧，SD失败或慢写不能覆盖USB帧。固定长度七缓冲稳定无借用时应为2 DMA、4 READY、1 FREE；持帧时相应减少READY。取走最新帧后不应回退到旧帧。JPEG按字节块与完整帧描述分别计数。
7. USB Alt0、拔插、Reset，与SD停止/写入交错时确认无重复释放；结束全部Camera前先结束存储。发生采集错误保留DRAINING直到全部引用归还。
8. 复杂画面IPC提示时保持运行观察，不能在暂停CM7后判断CM4失联。M4 peer_state=3与peer_age_ms>=1000说明心跳超时；state=4和protocol_errors增加说明协议错误。M7 max_step_gap_ms>=1000可支持任务运行延迟判断。业务fresh阈值仍500ms。

这些项目均待用户实际执行。若再次报错，在首次Dcmi_RecordError停下记录result、s_event_dma_error、DMA1->LISR/CR/FCR/NDTR及调用栈，不以继续运行后的清理结果代替首故障。
