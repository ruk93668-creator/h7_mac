# 320×240 JPEG → USB MJPEG 接入

以下为QVGA阶段历史记录。当前默认测试改为VGA，见`Tests/MjpegVga/README.md`；320×240能力保留。

本轮只接入QVGA JPEG，不开放640×480，不改变FIFO/DMA或USB传输状态机。独立03B分支开发，不合并main。

## 当前默认配置

- `APP_VIDEO_MJPEG_MODE=1`：320×240 JPEG，输出目标25 FPS，UVC最大帧38400B。
- `APP_VIDEO_MJPEG_MODE=0`：保留160×120 YUY2。
- JPEG单帧功能保留，API可接收160×120或320×240；原独立单帧验收应用的默认160×120不擅自替换。
- 所有格式只支持冷启动选择，未实现运行时切换。

## 修改与依据

1. Common统一JPEG尺寸接入范围160×120、320×240，其他尺寸明确拒绝。
2. Device显式映射`OV5640_R160x120`和`OV5640_R320x240`，将映射结果传入`OV5640_Init`。
3. ST组件已有QVGA表，对3808/3809写01/40，对380A/380B写00/F0；没有修改官方组件。
4. 保留原PCLK、HTS/VTS、待机和Device_Start顺序。后续时序表只写380C～380F，不覆盖宽高。
5. Media尺寸检查采用同一接入范围；JPEG SOF必须与本轮传入尺寸一致，不能拿160×120帧冒充QVGA。
6. UVC放开QVGA配置校验，描述符原有动态路径写入320、240、400000帧间隔和38400最大帧长；Probe/Commit沿用当前配置，不修改协议格式。
7. FIFO块、引用计数、两段帧视图、跨任务epoch/lease、Payload、SOF和EP1保持原实现。

38400B需要ceil(38400/1021)=38包，25FPS为950包/秒，通过现有每毫秒一包的配置上限检查，但余量有限。超过38400B的JPEG仍会被UVC提交接口拒绝，不通过截断、修改EOF或提前归还来发送。目标25FPS不等于实测25FPS。

## 本轮实际检查

```powershell
python Tests/MjpegQvga/source_audit.py
```

检查真实C代码的语法树、公共尺寸返回表达式、组件QVGA表、配置传递、USB预算及保留的算法函数体。语法检查使用占位类型，不具备真实头文件类型检查和链接能力。

结果见`source_results.json`。没有调用C编译器、Keil、可执行测试或实板，不声明构建/Windows视频通过。

FIFO隔离C测试增加一例：QVGA启动后注入原160×120样本，必须拒绝交付。原14例保留，总计15例，未编译、未执行。

## 用户编译与实板操作

1. 打开当前`MDK-ARM/h7_mac_stage03b.uvprojx`，选择`CM7_MJPEG`完整Rebuild；或者正式工程CM7全局开启`APP_VIDEO_MJPEG_MODE=1`。
2. 确认使用新AXF烧录，`init_error`应为0。观察Camera/USB任务结果及Control首次故障结果。
3. 拔插USB重新枚举，在电脑查看格式列表：

```powershell
ffmpeg -f dshow -list_options true -i video="Roxy Camera"
```

设备名称使用Windows实际显示的名称。应能列出MJPEG 320×240、25FPS。播放：

```powershell
ffplay -f dshow -video_size 320x240 -framerate 25 -vcodec mjpeg -i video="Roxy Camera"
```

4. 检查真实场景画面、颜色、完整性、连续运行和实际帧率；同时观察提交/归还计数及DMA错误。
5. 测试Alt0重开、USB Reset/Disconnect、Stop/Restart。若大帧提交返回INVALID_ARG，先记录实际JPEG长度，不能直接放宽USB带宽检查。
6. 原YUY2和JPEG单帧分别重编复验。硬件未验证前不宣称QVGA已完成验收。

USB识别出设备不等于视频取流成功；实际FPS和复杂画面下最大JPEG长度仍待测量。
