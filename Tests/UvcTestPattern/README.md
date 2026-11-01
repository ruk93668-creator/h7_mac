# UVC-09C：静态彩条与冷启动模式

基线 main `9ee405e465c8f29c9fe9e6665edb05bae7970c2a`，分支 `codex/uvc-09c-test-pattern`。本轮只创建审核 PR，不合并 main。软件仿真不代表 Windows 已显示视频。

## 启用方法

默认 `CM7/Service/usb_mode.h` 中 `USB_MODE_UVC_TEST_ENABLED=0`，保持原 `h7_mac USB FS Test`。开启静态彩条有两种等效方式，选择一种即可：

1. Keil **h7_mac_CM7 → Options for Target → C/C++ → Define** 追加 `USB_MODE_UVC_TEST_ENABLED=1`，不要修改 CM4 Target。
2. 将头文件默认值改为 1。

随后完整 Rebuild CM7，下载并冷复位；重新连接 USB。切回 TEST 时删除该 Define 或改为 0，并再次完整重编、下载、冷复位。只改变当前内存中的 mode 不会切换 Class；不支持 TEST/UVC/MSC 热切换。

本轮正式 Keil 工程保持默认 TEST。`build.ps1` 使用独立临时工程验证宏为 1 的 CM7，再完整构建 CM4 和默认 TEST CM7，不改正式工程的 Define。

## 初始化和运行关系

```text
DEVICE: USB_Device_Init
  → SERVICE: USB_Mode_Init
      → 未注册Class时，按编译期开关注册唯一TEST或USB_UVC_Register
  → APPLICATION: UsbMode_AppStart → USB_Mode_Start
      → WAIT_CONFIGURED → 主机SET_CONFIGURATION → RUNNING

APPLICATION: App_Init
  → TEST: 原App_CameraInit
  → UVC: 仅App_UvcTestInit，生成一次静态图像

Main_Run
  → 原Framework与USB Mode处理
  → USB_UVC_Process（09B SOF调度、09A分包和完成确认）
  → App_Run → TEST调用原App_CameraRun / UVC只调用App_UvcTestRun
      → GetStreamStatus.can_submit && !frame_owned
      → USB_UVC_SubmitFrame（再次在USB临界区验证条件）
```

Initcall 的 APPLICATION 同等级条目先后不作为依赖；图像 Init 不要求 USB 已枚举，实际提交必须等 Class 就绪。未对现有 Camera、DCMI、DMA、Media 建立新依赖；默认TEST保持原相机应用。UVC测试模式跳过相机Application调用，不启动采集，图像数据不取自摄像头；既有BSP/Device Initcall保持不变，原Framework其他初始化的硬件失败仍会按既有Fail-fast处理。

`USB_Mode_Init` 保留已有显式 Class 注册，不覆盖或叠加第二个 Class；正式启动流程只有 Mode Manager 负责选择。Init 幂等，重复 Start 拒绝。保留原 WAIT_CONFIGURED 超时、RUNNING 设备故障检查和同模式 500ms 非阻塞重连；不增加热切换功能。沿用的 RUNNING 分支不新增对 Reset/Disconnect 的自动重连决策；是否允许提交始终由真实 Core/Class 状态决定。

## 静态图像与所有权

图像位于 Application 私有静态 `s_test_frame[38400]`，4字节对齐，由 CM7 CPU/USB PIO 读取，不使用 USB 内部 DMA、SDRAM 或摄像头。默认 TEST 不编译图像、颜色表或应用状态，只保留无状态空接口，不分配彩条缓冲区。生成完成后不再写入，重复 Init 不重建。

160×120、YUY2，顺序 `Y0 U Y1 V`，每行320B。8条竖向彩条，每条20像素，各行相同。使用 BT.601 有限范围的整数样值，颜色顺序为白、黄、青、绿、品红、红、蓝、黑：

|颜色|Y0|U|Y1|V|
|---|---:|---:|---:|---:|
|白|235|128|235|128|
|黄|210|16|210|146|
|青|170|166|170|16|
|绿|145|54|145|34|
|品红|106|202|106|222|
|红|81|90|81|240|
|蓝|41|240|41|110|
|黑|16|128|16|128|

图像 CRC32 为 `FC92082C`，逐包结果见 `results.json`。这是明确生成的实验彩条，不是实板相机图像。

`can_submit` 在现有 `USB_UVC_GetStreamStatus` 的同一个 USB IRQ 临界区内生成：Configured、Commit有效、EP1打开、VS Alt1、无关闭故障、WAIT_FRAME且不持有上一帧。Application 不读取私有 Class 上下文。查询之后若发生 Reset/Alt0，Submit 内部仍会拒绝过时请求。

Application 不自行清除 busy/Token、不 Close/Abort 端点。Abort、Close、Reset失败时，Class/PIO/图像继续保留，`can_submit=false`；09B Process 或受控 Stop 确认安全后才释放。Reset/Disconnect成功释放 Class 后应用安静等待重新枚举、Commit和Alt1。没有 Class 时查询失败不等于由 Application强行释放底层资源；本模块从不改写该静态图像。

38400B = 75×510B + 150B，含2B Header后为75包512B和末包152B。只有末包EOF=1；真实完成确认后才翻FID和归还整帧。原09B由Commit的1000000×100ns换算100ms，第二帧首包与第一帧首包相隔100 SOF；Application不增加定时器或发送额度。

## 协议及主机兼容性

描述符、Probe/Commit、SET_INTERFACE和分包协议均沿用已验收实现：

|项目|当前能力/行为|
|---|---|
|设备/IAD|EF/02/01；VC0、VS1，UVC 1.1|
|配置描述符|180B；VC专用52B，VS专用77B|
|视频|YUY2，Format1/Frame1，160×120，10FPS，帧38400B|
|协商|34B Probe/Commit；实际参数与描述符一致|
|Alt1|有效Commit后打开EP1 IN 0x81，ISO，512B，bInterval=1|
|Payload|2B Header，FID/EOF/EOH；不启用PTS/SCR|
|吞吐|帧数据384000B/s；含每帧76个头为385520B/s，低于每毫秒510B图像净载荷上限|

本轮检查实际编译描述符、真实 Core 请求和输出包，未改变上述能力。标准 Windows UVC 驱动支持 Probe/Commit、等时传输及 YUY2，但这些支持并不能证明本板实际取流成功。[Microsoft UVC驱动说明](https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/usb-video-class-driver-overview)

TEST 与 UVC 沿用 `1209:0001`、bcdDevice=0100，不擅自申请或使用其他正式身份。该 PID 只允许内部实验、不唯一，不能用于分发或量产。[pid.codes 官方测试PID规则](https://pid.codes/1209/0001/)

Windows 根据 VID/PID、bcdDevice 等构造设备标识；两种固件类别不同，已有 TEST/WinUSB 手动绑定可能影响后续 UVC 的驱动选择。缓存冲突是需要在实板检查的风险，不能靠改产品名称保证隔离。[Microsoft 设备描述符与硬件ID说明](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/standard-usb-descriptors)

本轮选择不改变身份，通过断开、下载后重新插入并检查正确设备实例验证。若仍被识别为旧 TEST/WinUSB，先用 USBView核对当前描述符，再在设备管理器只卸载对应 `VID_1209&PID_0001` 的实验设备实例，重新插入并核对驱动；不要批量删除系统驱动。若需要另一种实验 bcdDevice/序列号隔离方案，应另行明确方案和兼容性边界，不能当作新的正式VID/PID授权。

## 软件验收与复现

```powershell
python Tests/UvcTestPattern/run_tests.py
python Tests/UvcTestPattern/run_regression.py
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/UvcTestPattern/build.ps1
```

使用 ARMCC5 V5.06 update7 build960 编译 ARM C99 代码，Unicorn 执行真实 Application、Mode、Class、Payload、Core、Platform、BSP。HAL寄存器/主机事件使用原有 Mock；原相机应用只做测试桩，不执行摄像头。独立编译 TEST/UVC 两个变体。

专项24项：默认TEST零生成零提交；显式注册与初始化顺序；幂等Init和重复Start；同模式500ms重连；拒绝热切换；无Configured/Commit/Alt1不提交；两帧完整包序列；借用期间不改写；Alt0/Reset/Disconnect恢复；Incomplete、Abort失败、Reset释放失败和受控Stop；中途BUSY；UVC应用不依赖Camera初始化。

监视真实函数入口，确认每个启动变体只执行一次成功的 `USBD_RegisterClass`；逐像素独立比对38400B；TEST变体额外断言ELF不存在图像、颜色表、生成函数和应用状态符号。Unicorn写监视确认生成器只写图像/自身栈、只生成一次、冻结后零写入；两帧逐字节核对Payload图像内容和EOF/FID。结果保留实际断言数量及失败行，不跳过旧用例。

回归执行原621次（Payload62×O0/O2=124、Streaming36、原USB461），并加09C专项24次。完整执行清单和控制台结果在 `regression_results.json`，逐例结果在 `results.json`，编译命令在 `commands.json`，Keil完整重编分别在 `cm7_uvc_keil.log`、`cm7_keil.log`、`cm4_keil.log`。

第一次回归发现原Core108失败：测试人为将TEST固件内部mode改为UVC，原启动检查会放行。已修正生产检查为仅允许编译期选定模式；没有修改原测试断言。修复后重新执行全部回归。此前进行中的构建被主动中断并从头完整重编，不用中断日志作为验收结果。

## 实板步骤与通过条件

1. 保留默认TEST先下载冷复位，确认原 `h7_mac USB FS Test` 仍可枚举。
2. CM7启用宏1、完整重编、下载并冷复位，按已有双核调试步骤运行两核，使用带数据线的USB连接电脑；不要只使用供电线。
3. USBView核对实际配置180B、VC/VS、YUY2 160×120、1000000间隔、EP81 ISO512、Alt0/Alt1。设备管理器检查视频功能使用 `usbvideo.sys`；若错误绑定，按上文只处理当前实验实例。
4. 用能显式选择YUY2 160×120、10FPS的UVC/DirectShow取流工具打开设备。Windows Camera界面未必展示该低分辨率/帧率，不能据此直接判断固件失败。
5. Keil观察 `s_usb_mode.mode/state/last_result`、Application的`s_pattern_ready/s_last_result`、UVC的`s_uvc_active->completed_frames/stream_state`。避免运行中频繁停CPU，断点会打断1ms传输节奏；观察后应重新开流测试。
6. 验证整幅图像为稳定竖向八彩条，无错行、紫绿交换或截断；检查主机实际帧率接近10FPS和长时间连续取流。软件两帧测试不替代这一步。
7. 关闭取流工具验证Alt0；再次打开验证重启；拔插、USB Reset或主机重新配置后重新Probe/Commit/Alt1，确认可恢复。硬件无法自然注入的Abort失败只记录软件故障注入通过，不声称实板故障注入通过。

默认TEST和UVC的完整重编要求均为0 Error/0 Warning。用于Keil观察的提交结果使用volatile静态变量，修复过首次构建“只写未读”的诊断告警；没有关闭编译器告警。

**未验证：**真实Windows枚举、驱动绑定和画面显示，实际USB时钟/电气、SOF响应延迟、总线丢包、长时间稳定性及多主机兼容性。保持原09B有限轮询和ISR时延边界，不宣称USB ISR完全无等待。

## 修改文件

|文件|原因|
|---|---|
|CM7/Service/usb_mode.c/.h|默认TEST与编译期UVC选择、唯一Class注册、冷启动约束、模式查询；保留非阻塞状态管理|
|CM7/Application/app_uvc_test.c/.h|独立静态图像和主循环借帧|
|CM7/Application/app_main.c|UVC冷启动只调用静态图像应用；默认TEST保持原相机应用调用及错误传播|
|CM7/Service/usb_uvc.h、usb_uvc_class.c|只扩展状态查询can_submit，不改协议、协商或调度算法|
|MDK-ARM/h7_mac.uvprojx|只在CM7 Application组加入新文件，CM4 Target原文不变|
|Tests/UvcTestPattern/|专项、全量回归入口、构建脚本、命令、结果和本报告|
|既有相关测试commands/results|实际重跑产生的新记录，不修改其C/Python测试主体和断言|

不修改BSP、Platform、HAL/LL、官方USB Core、UVC描述符、Payload算法、Camera、DCMI、DMA、Media或CM4代码；不修改main的output/、tmp/，也不包含这些目录或构建产物的提交。

## 最终验收结果

|检查|结果|
|---|---|
|09C专项|24/24 PASS（TEST 6项、UVC 18项）|
|原有回归|621/621 PASS，原C/Python测试主体和断言均未修改|
|总计|645/645 PASS，0 FAIL|
|CM7 UVC完整重编|0 Error、0 Warning|
|CM7 默认TEST完整重编|0 Error、0 Warning|
|CM4完整重编|0 Error、0 Warning|
|范围审计|8个生产文件；BSP/Platform/HAL/Core/描述符/Payload/CM4及原Camera模块无改动|
|实板|未执行Windows枚举和视频显示验收|

实际构建大小、静态图像地址及汇总见 `verification_results.json`；代码SHA、CM4 Target和main保护检查见 `audit.json`。本轮只提交开发分支并创建PR，等待审核；不合并main。

## PR #3 内存优化（基于171479f）

仅对app_uvc_test.c增加编译期隔离。宏为0时不编译彩条缓冲区、颜色表、生成器和应用状态；对外保留无状态空接口。宏为1时原生成及发送行为保持不变，不修改Payload、SOF、EP1、Probe/Commit或描述符。

原24项专项编号及621次回归均保留，共645/645 PASS。TEST原先检查38400B零值的断言，改为检查实际ELF不存在对应存储及生成器符号；UVC逐字节和两帧76包测试不变。正式TEST AXF也确认不存在彩条符号。

|配置|优化前ZI|优化后ZI|ZI减少|RW+ZI减少|
|---|---:|---:|---:|---:|
|CM7 TEST|55396B|17004B|38392B|38400B|
|CM7 UVC|55396B|55396B|0|0|
|CM4|2580B|2580B|0|0|

TEST的RW由668B降至660B，故ZI减少38392B、RW减少8B，链接器报告的RW+ZI合计减少38400B（37.5KiB）。UVC维持原38400B图像、160×120 YUY2、10FPS。三个配置完整重编均0 Error、0 Warning。明细见verification_results.json，原始日志已更新；未执行实板视频验收。不合并main，output/、tmp/保持原样。
