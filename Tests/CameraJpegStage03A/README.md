# Stage03A：OV5640 JPEG单帧采集

状态：软件实现与专项检查已完成；实板复测、真实文件导出和PC解码待完成。分支codex/camera-jpeg-stage03a，不合并main。

## 架构与接口

| 文件 | 职责 |
|---|---|
| Common/Inc/camera_config.h | 追加JPEG枚举，保留Y8/RGB565/YUV422原数值；IsVariableLength识别压缩格式，GetFrameSize对JPEG返回NOT_SUPPORTED且不改输出 |
| CM7/Device/Src/ov5640_device.c | JPEG明确映射OV5640_JPEG，复用未修改的ST组件配置与Device启停；本阶段只开放160x120 |
| CM7/BSP/bsp_dcmi.c/.h | 独立JPEG Snapshot启动及任务收尾，保留YUY2 FIFO/突发设置并在Stop恢复 |
| CM7/Media/Src/camera_capture.c | Snapshot记录硬件接收范围和实际有效长度，解析JPEG段、SOF/SOS/EOI，处理超时和失败恢复 |
| CM7/Application/app_camera.c | 显式允许JPEG走Snapshot，禁止把JPEG配置当固定长度Stream |
| CM7/Application/app_camera_jpeg.c/.h | 编译期开关控制的独立测试应用，仅CameraTask操作资源 |
| CM7/Application/Tasks/camera_task.c | 默认仍走原业务；仅显式JPEG测试配置时执行独立验收入口 |
| MDK-ARM/h7_mac.uvprojx | CM7 Application组加入JPEG测试模块，不修改用户既有编译选项和CM4 |

BSP新增接口：

```c
SysResult_t BSP_DCMI_StartJpegSnapshot(uint32_t *buffer, uint32_t capacity_words);
SysResult_t BSP_DCMI_ProcessJpeg(uint32_t *received_bytes);
```

调用环境为Camera任务、非ISR、全局中断开启。Start成功仅表示启动；Process返回BUSY继续推进，成功表示硬件已停止并恢复，才填写接收字节数。非BUSY错误必须执行Stop恢复，不得发布图像。BSP交付长度包含最多3字节Word尾部填充，Media裁定真实size_bytes。

## 数据与资源边界

- 仍使用已配置的4MiB非缓存SDRAM窗口，从0xD0000400接收；JPEG与Stream互斥。
- 单次DMA最多65535 Word，JPEG接收上限262140B，不把600KiB缓冲预留容量直接传给DMA，不实现分段。
- 容量恰好耗尽也保守拒绝，返回NO_RESOURCE；不冒险交付可能截断的帧。
- JPEG的frame_bytes初始为0，received_bytes是安全收尾后的Word接收范围，valid_bytes才是JPEG有效长度。
- FRAME只锁存输入结束并发唤醒提示，ISR不扫描JPEG。任务要求FRAME、CAPTURE结束、DCMI FIFO空、DMA EN真正清零、无错误和合法NDTR共同成立。
- JPEG仅本轮使用32位等宽直传和SINGLE访问，无DMA FIFO尾部残留；停止后恢复原YUY2 FIFO/INC4参数。
- Media解析Baseline JPEG结构；拒绝伪EOI、缺失SOI/EOI、越界段长、尺寸不符、非法SOS、超时和硬件错误。此检查不是熵解码器，不能替代PC解码。
- 当前YUY2五缓冲、帧租借、SOF调度、USB描述符与Probe/Commit均未修改。默认video_config仍160x120 YUV422、目标25 FPS。

## 独立实板测试

正常项目不要添加JPEG开关。独立测试项目显式使用：

```c
APP_VIDEO_TEST_MODE=1
APP_CAMERA_JPEG_TEST_ENABLED=1
```

第二个开关要求第一个同时为1，否则编译拒绝，避免用JPEG测试替换默认UVC视频业务。测试USB为基础TEST设备，不提供JPEG/UVC视频，不用ffplay判断此模式。

已生成的复测工程：

```text
MDK-ARM/h7_mac_jpeg_stage03a_retest.uvprojx
```

选择h7_mac_CM7目标；复测输出独立为：

```text
MDK-ARM/h7_mac_CM7_JPEG_RETEST/h7_mac_CM7.axf
```

退出旧Debug会话后下载并进入新会话，确认加载的是上述AXF。CM4代码未改，保留原正常CM4固件。

跨机器获取工程后可重新生成并编译：

```powershell
python Tests/CameraJpegStage03A/run_build.py --variant cm7_jpeg --retest
```

生成工程和固件不入Git。若Keil占用旧.d文件，可用`--isolated`构建全套独立验证目录；不删除或结束用户的Keil进程。正式UVC输出与JPEG输出分开。

## Watch与导出

运行后查看`g_camera_jpeg_test`：

| 字段 | 含义 |
|---|---|
| state | 0待启动、1采集中、2保持待导出、3失败、4已释放停止 |
| result | 初始化或采集结果，0为成功；必须同时看state，不能把尚未完成当成功 |
| frame_address | 实际文件起始地址，当前0xD0000400 |
| size_bytes | 真实JPEG长度，不能固定导出38400B |
| width / height | 应为160 / 120 |
| stop_attempted / stop_result | 只有attempted=1时stop_result才表示执行过应用清理，不拿默认0冒充成功 |
| release_request | 导出完成后置1，CameraTask释放READY并停止传感器 |

等待state=2、result=0、size_bytes>0再暂停CM7。Media保持READY，不自动覆盖文件。

在PC根据Watch里的真实长度生成SAVE指令，例如将下面n替换为实际值：

```powershell
python -c "n=1234; print('SAVE jpeg_frame.hex 0xD0000400,0x%08X' % (0xD0000400+n-1))"
```

把输出整行复制到Keil Command执行。这里1234只是示例，不是预期JPEG长度。使用简单相对文件名，通常输出到MDK-ARM目录，避免之前的绝对路径解析问题。区间是闭区间，末地址=起始地址+长度-1。

然后在工程根目录运行，将1234替换为同一个真实长度：

```powershell
python Tests/CameraJpegStage03A/verify_jpeg.py MDK-ARM/jpeg_frame.hex --length 1234 --source hardware
```

工具校验HEX校验和、地址连续性、长度、SOI/EOI，使用Pillow verify和完整load检查分辨率及解码，输出JPEG、PNG预览和含SHA256的JSON。`--source hardware`是用户对文件来源的声明，工具不会自行证明来源。

可再用FFmpeg严格检查解码输出是否有错误：

```powershell
ffmpeg -hide_banner -v error -err_detect explode -i MDK-ARM/jpeg_frame.jpg -frames:v 1 -f null NUL
```

导出和校验后再恢复运行，并将release_request设为1。不要在USB TEST模式中直接调用其他任务的相机API。恢复默认UVC时重新打开原h7_mac.uvprojx、选择CM7并Rebuild；不把JPEG固件当作25 FPS视频固件。

## 首轮实板出现的误报与软件修复

用户反馈result=0xFFFFFFFA(-6)，JPEG内存以FF D8开头，frame_seen=1，DCMI CR=0x000040FA：FRAME已到，CAPTURE已清零。截图NDTR仍非0，不能认定262140B已收满。没有取得完整JPEG文件，SOI本身不证明文件完整。

检查发现收尾代码主动关闭DMA，但完成中断仍开着；软件关闭也可能置TCIF，而原JPEG完成回调把所有TC都判成NO_RESOURCE。这是本次实现遗漏，不是证明缓冲太小。用户已退出首次断点，没有额外取得disabling和调用栈现场；修复后的实板结果仍待反馈。

修复仅限JPEG：在请求关闭前设置disabling，并屏蔽TC/HT，保留TE/DME；完成回调忽略主动关闭期的迟到完成，只有NDTR=0才认定容量TC。任务停止后的NDTR=0检查仍保留，真实容量耗尽不会被放过。新增测试走真实HAL DMA IRQ分发，覆盖软件关闭TC、迟到回调和真正耗尽。原Mock只模拟EN位清零，没有自动生成TCIF，解释了为何首轮单元测试没有发现该问题。

AN4031的4.1/4.8节说明相关DMA的停止TC行为；其适用器件列为F2/F4/F7，不把它冒充H747参考手册。当前H7 HAL的TC分支会在TCIE有效时调用XferCpltCallback，软件可达性由实际源码和回归测试确认。参考：https://www.st.com/resource/en/application_note/dm00046011-using-the-stm32f2-stm32f4-and-stm32f7-series-dma-controller-stmicroelectronics.pdf

## 软件验收与限制

接收机制审计见[HAL接收审计](HAL接收审计.md)。专项包括BSP32项、Media33项、Device7项、Application8项、PC工具9项；PC工具测试使用合成图像；Media测试32另使用本次真实实板数据，不将其余模拟测试当作硬件验收。

原有Y8/RGB565/YUY2五缓冲90项、采集生命周期72项、OV5640 Device生命周期32项、Native Runtime97项、SDRAM布局24项，以及USB描述符40、字符串8、EP0 IRQ4项已回归。历史其他旧夹具的不兼容/失败不属于本轮修复，不声称全仓历史测试全部通过。

构建及逐例结果见本目录JSON和日志。实板以下事项尚未完成：修复后固件成功交付JPEG、停止/错误恢复实测、默认YUY2/UVC25 FPS在本分支复测。真实导出文件已完成PC解码，详情见第二轮记录。不得根据软件PASS标记这些项目通过。

## 第二轮实板导出与EOI边界修正

通过Keil的_RBYTE逐字节读出SDRAM，原始1104B数据保存在Hardware/jpeg_received.bin，包含SOI、完整基线JPEG结构和EOI。按JPEG结构解析，第一幅图像在1085B结束。该文件已使用Pillow完整解码为160×120图像，见Hardware/jpeg_frame.jpg及jpeg_frame.png，画面很暗。此结果来自用户实板导出，不是生成图像。

旧解析器要求EOI后最多3个零字节，遇到本次19B尾部返回SYS_ERR_PROTOCOL。现在按段长度跳过元数据，在熵数据中正确处理FF00、重启标记后确定首个EOI，以该位置作为size_bytes。不会把尾部内容传给消费者，也不将这些内容解释为正常填充。SOI、SOF尺寸、SOS、容量、接收边界和硬件错误检查保留。原测试8/9分别改为验证非零尾部和较长尾部不会扩大交付长度；新增测试32直接使用本次真实导出数据验证1085B边界。

原始导出范围由之前记录的0x450确定，目前不能证明它与观察到的received来自同一轮采集，因此尾部的具体来源尚未确认。保留所有原始字节和哈希，不据此宣布DMA收尾计数已通过实板验收。

最新固件须重新烧录：预期g_camera_jpeg_test.result=0、state=2、size_bytes>0。每次JPEG长度可能不同，以本轮size_bytes为准。保持暂停后执行：

```
INCLUDE "D:/32/STM32H7/h7_mac/Tests/CameraJpegStage03A/dump_ready.ini"
```

该脚本读取本轮size_bytes并导出jpeg_ready_bytes.txt。若SAVE输出只有扩展地址记录和EOF而无数据记录，不能作为JPEG数据使用。不要复用上次固定1104B长度判断新帧。

本次已证明导出数据中的一幅真实JPEG可被PC解码；修正后固件状态、重新采集和停止恢复仍需实板确认。

## 工程整理更新

旧03A临时uvprojx/uvguix及输出目录已删除。单帧验收使用固定MDK-ARM/h7_mac_stage03b.uvprojx的CM7_JPEG_SINGLE Target；run_build.py兼容旧命令，但统一委托03B构建脚本，不再生成旧临时工程。原始硬件文件及历史验收报告保留。
