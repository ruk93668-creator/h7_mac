# UVC 描述符验收

对象：当前 `CM7/Service/usb_uvc_desc.c/.h`，保持 UVC 1.0、YUY2 160×120、10FPS。

## 导出接口

```c
uint8_t *USB_UVC_GetFSConfigDescriptor(uint16_t *length);
uint8_t *USB_UVC_GetDeviceDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
```

- FS 配置接口可直接用于后续 Class 的 GetFSConfigDescriptor 回调；返回静态指针和179字节长度。
- 设备接口可用于后续 UVC 设备描述符表的 GetDeviceDescriptor 回调；仅接受 USBD_SPEED_FULL，返回18字节。
- length 为空时返回NULL；设备速度不支持时返回NULL且长度为0。
- 返回数据不释放、不由调用者修改。此模块不创建或注册 UVC Class，也不替换当前 TEST 描述符表。

## 长度核算

| 范围 | 组成 | 有效长度 | 小端字段 |
|---|---|---:|---|
| VC 类专用 | Header 13 + Camera 18 + Processing 11 + Output 9 | 51 | 33 00 |
| VS 类专用 | Header 14 + Format 27 + Frame 30 + Color 6 | 77 | 4D 00 |
| Configuration | 配置头9 + IAD8 + VC标准接口9 + VC类专用51 + VS Alt0标准接口9 + VS类专用77 + VS Alt1标准接口9 + 端点7 | 179 | B3 00 |

VC/VS 类专用总长度不包含标准接口和标准端点。配置数组成员本身为179字节；外围存储结构增加1字节padding，占180字节，避免PIO最后一次32位读取越过已分配存储。设备描述符有效18字节，存储20字节。补齐字节不返回给USB Core。

## IAD 设备身份

独立UVC设备描述符设置 bDeviceClass/bDeviceSubClass/bDeviceProtocol 为 EF/02/01。依据：

- [USB-IF Defined Class Codes](https://www.usb.org/defined-class-codes)
- [Microsoft USB Interface Association Descriptor](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/usb-interface-association-descriptor)

暂沿用内部测试 VID/PID 1209:0001，字符串索引均为0；本模块不声明尚未提供的字符串。Platform 中现有 TEST 设备描述符、产品字符串和默认注册方式均不变。

## 自动检查

```powershell
python Tests/UsbUvcDescriptors/run_tests.py
```

实际使用 ARMCC5 V5.06 update7 build960 编译正式描述符源码，并在 Unicorn 执行导出函数；不是只用Python解析注释。HAL和硬件不参与该专项测试。运行器依赖现有 Tests/OledAsyncAcceptance/python_deps 下的 Unicorn、pyelftools；这些本机依赖不提交。

20/20通过，检查：

- 配置长度、14段bLength边界、接口编号与Alt设置、IAD。
- VC/VS类专用长度、UVC版本、Terminal/Unit唯一编号与1→2→3拓扑。
- Camera/Processing控制位图与保留字段。
- YUY2 GUID、小端字段、16bpp、分辨率、3072000bit/s、38400字节、1000000×100ns。
- Color Matching、EP1 IN、512字节包长。
- IAD设备身份、有效长度、静态指针、4字节对齐与尾部补齐。
- 空length、不支持的速度，以及179个显式初始化字节与实际返回数据逐字节一致。

实际命令、编译器版本见 commands.json；专项结果与源码哈希见 results.json。

另外运行既有 USB 枚举/Mode 测试及EP1检查，使用仅调整FIFO/端点索引的隔离HAL桩：100/100通过，2441项断言。实际命令为 `python tmp/UsbEp1Acceptance/run_tests.py`；相关脚本保留在当前本机tmp目录，不属于本专项测试的运行依赖。结果摘要存档为 regression_results.json。

Keil正式目标：CM7 0 Error、0 Warning；CM4 0 Error、0 Warning。日志见 cm7_keil.log、cm4_keil.log。保护范围40个文件SHA256均无变化，见 protected_sources.json，包括CM4、官方USB Core、现有Platform/Mode及BSP源文件。

## 未验收范围

- 未实现Probe/Commit、SET_INTERFACE处理或UVC发送，未注册新的UVC描述符表。默认仍运行TEST；不能据此宣称Windows已识别UVC摄像头。
- 后续Class需要组装完整的USBD_DescriptorsTypeDef表，将设备回调与Class配置回调一起注册。本轮提供两个导出函数，不提供完整字符串回调表。
- usbd_conf.h 的 USBD_MAX_NUM_INTERFACES 仍为1，本轮未修改。当前官方Core使用接口编号 <= 该宏判断，因此不能仅据宏值断言接口1会被拒绝；后续双接口Class接入时应统一接口数量配置并验证路由。
- 48MHz时间戳基准仍需后续PTS/SCR实现匹配。描述符声明10FPS不等于当前传感器及USB链路已实测10FPS；真实YUY2排列、色彩与传输时序尚待实板验收。

本轮没有修改USB Core、BSP发送、Mode、CameraCapture、CM4源码或Keil工程配置。
