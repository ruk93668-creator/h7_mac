# 冷启动视频配置

唯一业务默认入口：`CM7/Application/video_config.c`。

```c
static const VideoConfig_t s_default_config =
{
    .camera =
    {
        .width = 160U,
        .height = 120U,
        .format = CAMERA_PIXEL_FORMAT_YUV422
    },
    .output = VIDEO_OUTPUT_USB_UVC,
    .output_fps = 10U
};
```

## 配置传递

```text
video_config.c
    ├─ App_CameraPrepare → App_CameraConfigure → OV5640 / Media
    └─ App_UvcBridgePrepareOutput → UVC Configure / Bridge Configure / USB Mode
```

各 APP 独立读取默认配置，并向下传递公共结构体。没有 video_pipeline；OV5640、Media、UVC 不包含业务头文件 video_config.h。

- `CameraConfig_t`：宽、高、像素格式。
- `VideoStreamConfig_t`：输出图像配置和输出 FPS。
- `VideoConfig_t`：业务默认图像配置、输出类型和输出 FPS。
- 配置被下层复制或转换成自身参数，不长期借用 APP 的可变配置指针。

## 启动顺序

1. PLATFORM：USB 应用读取默认配置、校验 UVC 能力、填写描述符及选择模式；不启动硬件。
2. DEVICE：原有 USB Core 初始化。
3. SERVICE：原有 USB Mode 初始化并注册所选 Class。
4. APPLICATION：原有唯一 USB 启动入口启动 USB；原有 App_Init 调用相机连续采集入口。
5. 主循环沿用 App_CameraRun、App_UvcBridgeProcess 与原 USB Process 调度。

相机 APP 自行读取相同默认配置，完成 OV5640 配置及 Media 初始化。桥接只核对双方图像尺寸、格式和字节数，原帧借用与归还逻辑保留。

## 当前能力与限制

| 模块 | 当前能力 |
| --- | --- |
| OV5640 Device | 仅已验证的 160×120；Y8、RGB565、YUV422；其余尺寸返回不支持 |
| Media Stream | 按输入尺寸计算帧长度；3×40960B，保留 DMA、SDRAM、溢出及对齐检查 |
| USB UVC | YUY2，即 CAMERA_PIXEL_FORMAT_YUV422；一组尺寸、一种输出帧率，FS ISO 1023B |
| 输出 FPS | 描述符、Probe/Commit、SubmitFrame 长度和 SOF 帧间隔由同一份已接受配置派生 |

配置集中不等于底层已经支持任意分辨率。Device 中的 160×120 是已验证能力检查；缓冲区容量、端点包长是资源边界，继续由各模块负责。

**不设置传感器采集 FPS**：OV5640 原 PCLK、行帧时序和初始化寄存器顺序不变。output_fps 只约束 USB 输出，不能用于推断传感器实际 FPS。

UVC 每个 SOF 最多一个包；每包包含 2B 头，1021B 图像数据。配置检查要求 `ceil(frame_bytes / 1021) <= floor(1000 / output_fps)`，并检查位率溢出。160×120 YUY2 共 38400B、38 包；30 FPS 被拒绝。该检查是软件提交预算，不保证实板带宽或主机兼容性。

## 输出选择

- `VIDEO_OUTPUT_USB_UVC`：配置并启动 UVC，使用真实相机桥接。
- `VIDEO_OUTPUT_USB_TEST`：原 TEST Class；桥接不取帧。
- `VIDEO_OUTPUT_NONE`：不注册或启动 USB 输出；相机原启动方式保留。
- ETH、Wi-Fi：预留枚举，启动校验返回 SYS_ERR_NOT_SUPPORTED。

只改变 video_config.c 的默认值即可选择以上业务配置。usb_mode.h 的旧编译期开关仅作为独立 USB 测试缺少 APP 显式选择时的兼容后备，不覆盖正式 APP 选择。

## 冷启动约定

配置应在 Class 注册前提交；配置成功后不能重复 Configure。`VideoConfig_RequestUpdate(NULL)` 返回参数错误，其余运行时更新返回不支持。本轮不实现停采、格式热切换或重新枚举。

## 验证边界

软件验证采用 ARMCC5 编译真实 C 源码和 Unicorn Cortex-M 执行，HAL/外设由隔离测试桩提供。软件通过不表示新的输出 FPS 已实板通过；传感器输出、SDRAM 并发及 Windows 取流需实板验证。详见 Tests/VideoConfig/README.md。
