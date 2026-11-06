# 640×480 JPEG → USB MJPEG 测试接入

本轮基线：03B分支4b3a7a0。保留160×120、320×240 JPEG和原YUY2功能；不合并main，不代用户编译或操作实板。

## 当前测试配置

| 项目 | MJPEG模式 | 原YUY2模式 |
|---|---|---|
| 尺寸 | 640×480 | 160×120 |
| 目标输出FPS | 10 | 25 |
| 最大JPEG帧 | 70000B | 不适用 |
| USB端点 | FS ISO IN 0x81、1023B | 保持原设置 |

配置仍位于`CM7/Application/video_config.c`。目标10FPS不是实测结果。

## 修改点

- Common JPEG尺寸表增加640×480；原160×120、320×240保留。
- Device增加明确的`OV5640_R640x480`映射，复用ST VGA寄存器表。
- ST VGA表写3808/3809为02/80，380A/380B为01/E0，输出尺寸确实为640×480。
- Media、FIFO和UVC已使用公共尺寸校验，因此同步接受VGA，不重复写尺寸常量。
- 沿用原时钟、行帧时序、Device_Start顺序、JPEG质量参数；不修改官方组件。
- JPEG Snapshot接口保留，独立单帧测试应用默认160×120不变。
- 不开放640×480 YUY2：原单次DMA整帧接收与USB带宽仍有限制。

## 长度与带宽

每个USB包含2B UVC头，可携带1021B JPEG。70000B需要69包，10FPS为690包/秒，通过现有每SOF最多一包的配置检查；70000B、25FPS不通过该检查。

JPEG按真实长度发送，超过70000B的帧会被拒绝并走原归还协议，不截断、不提前回收。JPEG大小随画面变化，70000B只是本轮声明上限，不保证所有场景都能压到此范围。

接收端仍是512KiB FIFO、8KiB DMA块和8项帧描述队列，地址与MPU不变。VGA JPEG可跨多个DMA块，单帧软件上限仍为262140B。未修改FIFO压力处理、两段视图、epoch/lease、SOF和Payload协议。

## 本轮检查

```powershell
python Tests/MjpegVga/source_audit.py
```

实际结果见`source_results.json`。检查C99语法树、真实尺寸返回表达式、ST QVGA/VGA寄存器表、USB包数预算、默认配置和原算法保留。语法检查使用占位类型，不能替代ARMCC真实类型检查、链接、可执行测试和硬件验收。

FIFO隔离C测试新增VGA启动及SOF尺寸不匹配拒绝用例。原用例保留，共16项待用户编译执行。本轮没有运行C编译器或Keil。

## 用户测试步骤

1. 在当前工程完整Rebuild `CM7_MJPEG`；确认全局有`APP_VIDEO_MJPEG_MODE=1`，没有开启独立JPEG单帧测试。
2. 烧录本轮新AXF，确认`init_error=0`、Camera/USB任务结果正常；拔插USB重新枚举。
3. 查看Windows实际格式列表：

```powershell
ffmpeg -f dshow -list_options true -i video="Roxy Camera"
```

4. 选择MJPEG 640×480、10FPS：

```powershell
ffplay -f dshow -video_size 640x480 -framerate 10 -vcodec mjpeg -i video="Roxy Camera"
```

名称以Windows实际设备名为准。确认真实场景、尺寸、颜色、画面完整性和连续运行，再记录Camera、USB及主机实际FPS。USB模式配置值不当作实测FPS。

5. 若画面不出或提交失败，观察首次错误与JPEG真实长度，特别检查是否超过70000B。不能为通过配置校验而直接放宽带宽限制。
6. 测试关闭重开、Reset、Disconnect、Stop/Restart，并回归原YUY2和JPEG单帧。

尚未完成：ARMCC编译、VGA实板图像、实际FPS、复杂场景最大帧长、DMA FIFO稳定性及USB恢复验收。不得将源码检查PASS视为实板视频通过。
