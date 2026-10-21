# 实板相机图像导出与查看

工具只处理输入文件，不生成替代测试图。正式BSP、Device、Media、Application不修改。

## Keil导出

1. 使用实板调试，等待 `g_camera_check.finished=1`、`success=1`。
2. 暂停CPU，不Reset、不重新下载，保留SDRAM中的本帧。
3. 在Keil的Command窗口执行：

```text
SAVE camera_frame.hex 0xD0000400,0xD0004EFF
```

当前工程的实际输出位置为 `MDK-ARM/camera_frame.hex`。本次已从该路径读取到用户提供的实板导出。

Keil SAVE输出HEX386格式，不是原始BIN；不要只改扩展名。相对文件名避免了本次绝对路径被解析异常造成的error 64。参考：[Keil官方SAVE说明](https://www.keil.com/support/man/docs/uv4cl/uv4cl_cm_save.htm)。

范围为0xD0000400～0xD0004EFF（两端包含），共19200字节。

## 转换

在工程根目录执行，无需安装第三方库：

```powershell
python Tests/CameraVisualization/view_frame.py MDK-ARM/camera_frame.hex --expected-crc 0x35A2952B
```

`--expected-crc`应填写当前这帧Watch中的CRC；上面的数值来自本次已通过单帧采集的截图。后续帧以实际Watch值为准。也支持真正的19200字节BIN作为输入。

每次输出到 `Tests/CameraVisualization/captures/日期时间/`：

- source.hex或source.bin：来源文件原样归档。
- frame.bin：精确19200字节原始像素，未补齐、裁剪、翻转或归一化。
- frame_160x120.png：160×120、8位灰度，像素(x,y)对应BIN的第y×160+x字节。
- frame_preview_4x.png：640×480最近邻放大，仅方便查看。
- analysis.json、验收记录.md：长度、校验、逐行特征和验收记录。

HEX导入会校验记录长度和校验和，拒绝缺失数据、冲突地址和缺失EOF，不以0或FF补齐缺失字节。BIN长度不等于19200则拒绝转换。CRC不匹配仍保留真实数据和图像，但命令返回2并明确记录不匹配，不能算一致性通过。

图像内容必须再目视检查；统计不会自动证明彩条正确。垂直彩条各行完全相同可以是正常现象，不能仅凭重复行判定DMA故障，也不能借静态彩条单独证明所有行均独立采得。

## 本轮记录

本次实际数据和可视化结论见本目录的 `验收报告.md`。工具检查命令：

```powershell
python Tests/CameraVisualization/test_view_frame.py
```

11项检查使用归档的真实帧核对PNG逐像素还原；临时损坏的HEX/BIN只用于验证拒绝逻辑，不作为实板图像或验收结果。测试校验PNG使用Pillow，转换脚本本身仅依赖Python标准库。
