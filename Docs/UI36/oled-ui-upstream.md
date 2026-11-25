# OLED_UI 固定参考与功能对照

参考 [bdth-7777777/OLED_UI @ 8f02069](https://github.com/bdth-7777777/OLED_UI/tree/8f02069f890e5ac074b41100fb0651887c821ce8)。检查了 Core 的 OLED_UI.c/.h、OLED_UI_MenuData.c/.h、Software_Driver OLED.c/.h、OLED_Fonts.c/.h、misc.c/.h，以及 STM32/MSPM0 示例目录和相关配置。表中“通过”仅指软件或视觉证据。实板所有项仍须验收。

| 参考功能或示例 | STM32H747 对应实现与差异 | 验证或剩余项 |
|---|---|---|
| MENU_TYPE_LIST、长列表、行前缀、布尔/数值条目 | SYSTEM 与 UI Tools；标题+可见区、滚动条、方向和复选前缀、光标、独立值编辑 | 70–71、86–87 |
| MENU_TYPE_TILES、位图/GIF 图标、横向移动 | UI Tools 的 Tiles，32×32 图标缩为 16×16，每屏三块；Gallery 播放原数组存在的 30 帧，不按参考的硬编码上限读取 | 87 帧 12–14、24–25 |
| MENU_TYPE_EMPTY、空白与辅助绘制回调 | Empty Page、Gallery、局部列表和辅助图形页。主循环固定动作编号派发，保持同一父页和返回语义 | 87 帧 24–31。未提供参考回调 ABI 兼容层 |
| 六种 ReverseCoordinate 光标 | 实心反色、圆角反色、矩形轮廓、圆角轮廓、底边反色条、隐藏 | 87 帧 6–11，亮像素逐帧不同 |
| ChangeFloatNum、MoveMenuElements | 线性、缓出、定长阻尼弹簧响应；定点时间插值、直接采样最新状态 | 77、86–87。PID 风格在视觉上保留欠阻尼，未照搬不稳定积分项 |
| SetTargetScrollBarHeight / 滚动窗口 | 工具列表右侧滚动条；屏高变化按可见区算行数，128×32 的 16/20px 显示一行 | 70–71、87 |
| Text_String、Short/Long 模式、LineSlip、PrintfMixArea | UI Tools 的像素滚动、只读文字弹窗及本工程状态文本；二维局部列表剪裁，混排支持参考 GB2312 范围 | 77、86–87。未移植通用 printf 接口 |
| 屏幕进退场与 Fadeout、页面切换 | 原菜单标题过渡，工具视图使用五级有序遮罩和时间采样 | 77、87。工具渐显取当前状态，未保留旧页面的逐级渐隐 |
| Window_RECTANGLE / ROUNDRECTANGLE、文本/空/整数/浮点 | 实现矩形/圆角、文字与空窗口、整数时长及定点小数速度；进场、进度、确认和取消；值只写 UI 私有设置 | 78、86–87 |
| BoolRadioBox、List_IntBox、List_FloatBox、子页与回调 | 灯色和帧率开关；编辑以只读描述符、固定动作编号和暂存值处理，选择/返回只改变 CM4 UI | 86–87。未提供外部业务任意指针或回调注册 |
| 8/12/16/20 ASCII、中文及混排 | 保留参考字库的字号、步进和像素，未收录项回退问号。UI Tools 用可用 24 行高度布局 | 86、87 帧 15–17、25。系统状态页仍沿用 UI1 8px 行布局 |
| Contrast/Brightness、Sleep | **BLOCKED**：冻结 Device 没有对比度或休眠信令接口。不由 UI 发字节给 I²C | 内部动画时长编辑不是亮度设置 |
| LightMode、Show FPS、设备/作者 About | 主题反色、基于真实完成帧间隔的 FPS，About 页面提供平台与来源提交 | 86–87 帧率软件入口；实际 FPS NOT_RUN |
| Calc / Yaw / Pitch / Roll 示例 | **BLOCKED**：CM4 现有只读 IPC 不发布姿态，无法将参考示例占位数当真实值 | 可复用编辑和窗口机制，未仿制传感器数值 |
| Alipay、WeChat、QQ、LOGO、QR 示例素材 | 保留通用静态 Bitmap 与 GIF 显示机制，没有植入付款目的地、商标或参考项目宣传内容 | 素材内容未复制，功能对照不计它们为设备功能 |
| OLED.c 的数字、文本、裁剪、线框等基础绘图 | UI1 Renderer 负责像素与区域基础，混合字库和 Bitmap 在 Application；Shape Gallery 展示三角形、圆、椭圆和弧 | 0–68、86–87。未复制通用格式化、填充扇弧和任意角度绘制 API |
| misc.c/.h 的按键示例、阻塞式和平台初始化 | 参考 misc 包含注释掉的按钮示范。继续使用 H747 U7、输入队列、两个内核的原 HAL 与 Framework | UI3 模拟；硬件不同，原 IRQ/驱动不复制 |
| Software/Hardware 驱动、阻塞刷新、启动与硬件示例 | 现有 SSD1306 128×32 I²C2 Device、四页异步顺序，不移植其他控制器、SPI 或外部总线 | 72、81–82，20 FPS 实板证据 NOT_RUN |
| System Monitor（用户追加） | 已接入 Tasks/Memory/CPU/Runtime。用有界注入模型验证，不占用共享内存 | 83–85。CM7 发布和采集开销 **BLOCKED**，见 [通路方案](system-monitor-design.md) |

保留 [Apache-2.0 原许可证](OLED_UI.Apache-2.0.txt)。随本工程分发的上游衍生资源仅 `oled_ui_assets.inc`。素材取自参考 `OLED_Fonts.c`，保留 95 个 ASCII 字形与参考中文表数据，中文索引改为显式编码整数，像素数据按原页布局。选用的五类通用图标及 cube 动画均以 2×2 像素并集缩小成 16×16，改为行对齐 1bpp。未复制参考 `OLED_UI.c`、`OLED.c` 或硬件接口、IRQ。源码中的功能差异、未对齐项和硬件冻结项仍待审核，不将此矩阵解释为完整等价验收。
