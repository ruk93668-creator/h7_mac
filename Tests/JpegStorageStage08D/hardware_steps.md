# Stage08D实板验收步骤

本阶段实板尚未执行。先备份卡内重要文件；本固件不格式化、不建分区、不自动删除文件，但请求录像/抓拍会正常修改FAT目录和文件数据。

## 1. 工程与镜像

隔离工程：D:/32/STM32H7/h7_stage08d/MDK-ARM/h7_mac.uvprojx，分支codex/stage08d-jpeg-storage。原D:/32/STM32H7/h7_mac仍保留08C和用户设置。不要混用两个目录的AXF符号。

正式Target h7_mac_CM7默认仍是YUY2兼容配置，不能保存伪JPG。要测JPEG，在你自己的Keil C/C++ Define中添加APP_VIDEO_MJPEG_MODE=1，保留其他原定义，完整Rebuild再Flash Download同一Target，不改变Output目录/名称。默认JPEG为640x480、目标USB10FPS、每帧上限70000B，实测FPS另记。若选择320x240，改video_config.c的JPEG尺寸配置并完整编译，不能把尺寸配置值当作实测性能。

软件验收额外镜像在output/Stage08D/profiles/，是复用已全量构建对象、仅重编配置再链接的独立配置验证，不是三个Target各自全量重编。无需为实板另建测试工程。CM4使用同分支正常Target配套固件。完整断电启动后先确认Camera/USB/IPC/OLED原功能。

## 2. Watch

添加g_storage_debug、g_camera_task_debug、g_usb_task_debug、g_bridge_debug、g_ipc_task_debug（若该名称与头文件不符，以Tasks/ipc_task.h实际声明为准）。

ready应为1，初始state=0、request=0，不自动识卡写文件。保存请求只在当前Camera配置为JPEG时支持；TEST模式拒绝。请先确认Camera采集正在运行。

request是人工业务控制，不是已删除的SdTest或压力入口：

| request | 作用 |
|---|---|
| 1 | 开始录像 |
| 2 | 请求一张完整JPEG抓拍 |
| 3 | 停止新复制，排空并关闭录像/文件 |
| 4 | 故障后显式恢复，不保证故障文件完整 |

请求消费后自动清零。state：0空闲，1录像，2抓拍，3收尾，4故障。Start/Stop请求值被清零不等于操作成功，必须看state、result、cleanup_result、saved_files等。

## 3. USB-only

不设置保存request。Windows打开视频，记录至少一分钟Camera和USB实际FPS、FIFO dropped_frames增量、心跳变化及各任务栈余量。Storage不得创建文件、saved_files应不增加。必要时与08C同配置对照，不把分辨率/FPS目标当实测值。

## 4. 抓拍与USB+SD

USB视频运行时，将g_storage_debug.request=2，继续运行。预期state最终0，result=cleanup_result=0，saved_files和saved_frames各增加1，last_size>0且<=70000，last_crc=read_crc。frame_id/stream_epoch非0。文件名IMG00001.JPG随file_number递增，不覆盖原文件。

state=4时保存现场：result、cleanup_result、filesystem各字段、队列深度及复制耗时。无输入约5秒会报超时，不能当成生成了图像。恢复故障前先排除掉卡等实际原因，再request=4；恢复成功后重新触发业务请求，不自动录像。

正常完成且已卸载后取卡，在电脑打开JPG，并检查像素尺寸和文件大小，使用任意可信CRC32工具核对last_crc。电脑独立打开和CRC结果回报后，才记录真实JPEG保存通过。

## 5. AVI录像

将request=1，state应变1；运行10秒后request=3，等待state=0、result=cleanup_result=0、queue_depth=0、saved_files增加。打开VIDxxxxx.AVI前必须正常结束，saved_frames增长不等于文件已封尾。

当前卷8MiB、默认4MiB分段/保留512KiB，会自动因空间或限制正常结束，不自动清理旧文件。stop_reason=1主动、2总量/段数、3预留空间、4一般故障、5抓拍完成、6超时。空间正常到限与真正I/O错误分开记录。

正常卸载后在电脑执行：

```powershell
ffprobe -v error -count_frames -show_entries stream=codec_name,width,height,avg_frame_rate,nb_read_frames -of json X:\VID00001.AVI
ffplay X:\VID00001.AVI
ffmpeg -v error -xerror -i X:\VID00001.AVI -f null -
```

用实际盘符/编号替换X和文件名。验证MJPEG、尺寸、帧数、持续播放与正常跳转。AVI平均播放FPS根据本段已写帧和时间回填，不能据此代替Camera/USB/Storage的独立实测FPS。

## 6. SD-only

video_config.c中将主输出改VIDEO_OUTPUT_NONE，保持JPEG配置，重新全量编译并烧录；USB保持断开，Camera仍为SD待命。request=2抓拍或1录像，3正常停止。不要选择Ethernet/Wi-Fi，它们在本阶段明确不支持。

## 7. 并行与故障

USB+SD运行至少5分钟，分别记录Camera/USB/Storage实际FPS、FIFO和存储丢帧增量、queue_high_water、max_copy_ms、max_write_ms、IPC心跳和栈余量。毫秒计时含抢占/校验等工作，不能当作纯SD总线速率。pool满可丢存储帧，但不得出现USB错误归还或Media覆盖。

关闭播放器、Alt0、USB拔插时，录像可继续，USB恢复后核对Return/ACK计数。停止整个Camera前先正常停止Storage。写入掉卡/写满故障仅在可牺牲测试卡上由用户自行安排；软件已做Mock注入，未替用户执行破坏性测试。出现FAULT不允许强制清空所有权或误报保存成功，恢复不得删用户文件。

## 待回报

JPG电脑解码/大小/CRC；AVI电脑解码、帧数、时长及分段；三种场景实际指标；无卡/恢复；长时间运行。以上在用户反馈前均为未验证。
