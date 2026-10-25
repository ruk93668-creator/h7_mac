# USB Device第一阶段移植与验收

日期：2026-10-25。范围仅为CM7 USB Device BSP、ST Device Core、Platform适配及EP0基础枚举；不实现UVC。

## 硬件证据与选择

用户提供`SVG_Schematic1_2026-10-25.zip`，原文件SHA256见hardware_evidence.json。保留电源、主控两页SVG及阅读用PNG，分别为schematic_1、schematic_2。

- 电源页：USB1为USB-C 16Pin，正反面D+/D−并联，经USBLC6-2SC6连接USB_DP/USB_DM；CC1、CC2各5.1kΩ下拉。保护器件不是ULPI PHY。
- 主控页：USB_DM→PA11，USB_DP→PA12，芯片为STM32H747XIH6；未发现外部ULPI PHY及其数据/时钟总线。
- 选择USB2_OTG_FS（OTG2），内部Full-Speed PHY，12Mbit/s；不是USB1 HS+ULPI。
- VDD33USB与VDD50USB接VCC。电源页VBUS经TPS54302降为VCC（标注3.3V）；按直接3.3V供电关闭内部USB稳压器，启用USB电压检测器。
- VBUS没有连至PA9；PA9、PA10均未作为USB检测/ID信号接出。因此不配置VBUS/ID引脚，vbus_sensing_enable=DISABLE。
- 原理图不代替实物：VCC实际电压、焊接、USB供电能力、线材、ESD和插拔行为均待实板验证。

芯片资料：ST DS12930，https://www.st.com/resource/en/datasheet/stm32h747ig.pdf 。两套OTG均有内部FS PHY，HS1的480Mbit/s需要外部ULPI。VBUS检测与供电条件参考ST AN4879：https://www.st.com/resource/en/application_note/an4879-introduction-to-usb-hardware-and-pcb-guidelines-using-stm32-mcus-stmicroelectronics.pdf 。

## 修改文件

|文件|职责|
|---|---|
|CM7/BSP/bsp_usb.c、bsp_usb.h|USB2 FS时钟、GPIO、PCD、FIFO、NVIC、IRQ及通用事件/端点API|
|CM7/BSP/bsp_usb_hal_config.h|只在CM7通过Keil预包含启用HAL_PCD_MODULE_ENABLED，避免直接改CubeMX HAL配置|
|CM7/Platform/usb_device.c、usb_device.h|官方USBD_LL适配、PCD事件到Core、设备生命周期、Class注册、基础描述符及无业务枚举Class|
|CM7/Platform/usbd_conf.h|官方Core配置、静态内存与日志配置|
|Middlewares/ST/STM32_USB_Device_Library/Core/Inc、Src|ST官方USB Device Core，原样复制，包含标准请求和EP0控制状态机|
|Middlewares/ST/STM32_USB_Device_Library/LICENSE.txt|原ST许可|
|MDK-ARM/h7_mac.uvprojx|CM7源文件、Include Path、PCD预包含；CM4明确排除新增USB驱动和Core|
|Tests/UsbDeviceStage1/|原理图证据、模拟测试、构建日志、审计及本报告|

ST Core复用自本机旧工程`D:/codex_proe/h7_codex/h7_mac/Middlewares/ST/STM32_USB_Device_Library`，仅导入Core与许可证，未导入旧CDC业务。middleware_hashes.json逐文件记录哈希，source_audit.json确认未修改官方实现。

未改CameraCapture、Media三缓冲、DCMI BSP、Application、OV5640、IPC、CM4 OLED、HAL源文件、Scatter及MPU。既有文件仅改Keil工程配置，初始及最终源码审计见baseline.json与source_audit.json。

## 关键配置

|项目|值与依据|
|---|---|
|控制器/PHY|USB2_OTG_FS / PCD_PHY_EMBEDDED|
|速度|PCD_SPEED_FULL，12Mbit/s|
|GPIO|PA11 DM、PA12 DP，AF10_OTG2_FS，推挽/无上下拉/Very High|
|时钟|独立HSI48；USBSEL=HSI48；HAL名义频率检查48MHz|
|精度同步|CRS自动跟随USB2 SOF，1kHz，Reload=47999，默认误差门限与Trim；HAL保留芯片修订版本适配|
|原有时钟|不改PLL1/PLL2/PLL3参数或公共输入源，保持M7/M4、相机XCLK、SDRAM时钟|
|VBUS/ID|均不配置；未接MCU检测线，关闭VBUS sensing|
|PCD|USB DMA关闭、LPM关闭、低功耗关闭、SOF CPU中断关闭；CRS同步不依赖PCD SOF回调|
|NVIC|OTG_FS_IRQn优先级6，低于CM7 DCMI/DMA的5，高于SysTick的15；Init不启用IRQ，Start后启用，Stop/DeInit关闭并清Pending|
|FIFO|Rx=128 Word，Tx0=64 Word，合计768字节；未分配非零端点FIFO|
|EP0|IN/OUT均64字节；本阶段拒绝打开非零端点|
|配置|一个Vendor接口0、无非零端点、无视频能力；总长18字节，bus-powered，MaxPower=500mA|
|测试身份|VID 1209 / PID 0001，设备版本0100，仅内部测试；序列号H7MAC-STAGE1非唯一|
|内存|全静态；USBD/PCD及Class池位于现有RW_IRAM1 DTCM；描述符与字符串对齐；固定Class池1024字节，单块分配，不调用malloc|

1209:0001是pid.codes公开的内部测试PID，不唯一，禁止用于销售、生产或分发设备；产品阶段需更换合法且唯一的身份：https://pid.codes/1209/0001/ 。当前不是CDC或UVC设备，Windows不应预期出现COM口或摄像头。

## 初始化与公开API

Framework保持原等级顺序：

```text
BUS          BSP_USB_Init：时钟/GPIO/PCD/FIFO就绪，不连接主机
DEVICE       USB_Device_Init：绑定BSP回调，USBD_Init，注册基础枚举Class
SERVICE      为未来UVC Class注册预留，不实现UVC服务
APPLICATION  Usb_AutoStart→USB_Device_Start→USBD_Start→BSP_USB_Start
```

Platform接口：USB_Device_Init、USB_Device_RegisterClass、USB_Device_Start、USB_Device_Stop、USB_Device_IsConfigured、USB_Device_GetLastResult。

- 后续Service可在SERVICE等级调用USB_Device_RegisterClass，传入静态USBD_ClassTypeDef及USBD_DescriptorsTypeDef；只能在初始化后且停止时注册，之后由APPLICATION等级启动。
- 此接口不意味着视频端点已实现。后续需设计UVC描述符、控制状态机、FIFO/端点资源和发送流程。
- BSP私有PCD句柄不向Platform公开；通过BspUsbCallbacks_t及BSP_USB端点操作隔离Core。
- Stop保留底层真实错误，避免官方USBD_Stop忽略LL返回值导致假成功；重新Start先清除旧CONFIGURED状态，等待主机重新枚举。

## 完整枚举调用链

```text
主机Bus Reset
→ OTG_FS_IRQHandler
→ HAL_PCD_IRQHandler（正式HAL）
→ HAL_PCD_ResetCallback（BSP）
→ Usb_Reset（Platform）
→ USBD_LL_SetSpeed(FULL)、USBD_LL_Reset（官方Core）
→ USBD_LL_OpenEP（Platform）
→ BSP_USB_OpenEp→HAL_PCD_EP_Open，打开EP0 OUT/IN

主机SETUP包
→ OTG_FS_IRQHandler→HAL_PCD_IRQHandler
→ HAL_PCD_SetupStageCallback（BSP）
→ Usb_Setup（Platform）
→ USBD_LL_SetupStage（官方Core）
→ USBD_ParseSetupRequest、USBD_StdDevReq（官方Core）
  ├─ GET_DESCRIPTOR→USBD_GetDescriptor→设备/配置/字符串回调
  │  →USBD_CtlSendData→USBD_LL_Transmit→BSP_USB_Transmit
  │  →HAL_PCD_EP_Transmit→USB_EPStartXfer/PIO FIFO（官方LL）
  ├─ SET_ADDRESS→USBD_SetAddress→USBD_LL_SetUSBAddress
  │  →BSP_USB_SetAddress→HAL_PCD_SetAddress，随后EP0状态阶段
  └─ SET_CONFIGURATION→USBD_SetConfig→USBD_SetClassConfig
     →基础Class Init→EP0状态阶段→USBD_STATE_CONFIGURED

EP0数据/状态完成
→ HAL_PCD_DataInStageCallback / HAL_PCD_DataOutStageCallback（BSP）
→ Usb_DataIn / Usb_DataOut（Platform）
→ USBD_LL_DataInStage / USBD_LL_DataOutStage（官方Core）
→ 官方EP0状态机推进后续数据或状态握手
```

本阶段Class不处理UVC Probe/Commit、视频SET_INTERFACE及视频发送；这些请求STALL。设备标准请求由ST Core保留原实现，不手写另一套枚举协议栈。

## 内存与Cache检查

- PCD的dma_enable=DISABLE，USB数据由CPU读写OTG FIFO，USB硬件不直接访问DTCM，因此现有DTCM缓冲区可用。
- 无外设DMA和CPU之间的共享内存一致性需求，本轮不增加Cache清理、不修改MPU/SDRAM分区。
- 未来开启USB DMA或零拷贝发送时必须重新审核DMA可访问区域、32字节对齐和Cache维护，不能直接沿用本轮假设。
- 本轮未将摄像头Framebuffer交给USB，无摄像头所有权或数据面改动。

## 实际测试与构建

```powershell
python Tests/UsbDeviceStage1/run_tests.py
python Tests/BspMediaContinuous/run_tests.py
python Tests/Ov5640Migration/run_cm4_regression.py
python Tests/CameraFunctionLayout/Sdram/run_production.py
D:\32\UV4\UV4.exe -b D:\32\STM32H7\h7_mac\MDK-ARM\h7_mac.uvprojx -t h7_mac_CM7 -o D:\32\STM32H7\h7_mac\Tests\UsbDeviceStage1\cm7_keil.log
D:\32\UV4\UV4.exe -b D:\32\STM32H7\h7_mac\MDK-ARM\h7_mac.uvprojx -t h7_mac_CM4 -o D:\32\STM32H7\h7_mac\Tests\UsbDeviceStage1\cm4_keil.log
```

ARMCC 5.06 update7 build960；真实BSP、Platform与官方Device Core编译为ARM代码，由Unicorn执行。HAL PCD/RCC/GPIO/NVIC在USB隔离套件中使用可控桩，不模拟物理总线；真实PCD/PCDEx/LL USB由正式Target编译和链接。

|验收|实际结果|
|---|---|
|USB隔离测试|52/52 PASS，428次断言；初始化失败、时钟/GPIO/NVIC参数、回调隔离、Core请求、EP0数据/状态阶段、Class注册、固定内存、启动停止及错误传播|
|CM7正式Target|0 Error、0 Warning|
|CM4正式Target|0 Error、0 Warning；USB Core及PCD/LL明确不参与CM4构建|
|BSP–Media联合回归|108项符合基线预期：96项功能通过、12项既有边界风险成功复现；22614次断言，不代表风险已消除|
|OLED回归|12/12 PASS，3493次断言|
|SDRAM回归|20/20 PASS，531次断言|
|现有源码与ST Middleware审计|通过，仅Keil既有配置变化；正式相机/IPC/OLED等源文件未变；官方Core哈希不变|

首次CM4构建曾因Keil跨Target同步新增Group而尝试编译USB Core，已通过显式CM4排除修复；最终日志为上表结果。

## 实板待验

1. 确认PA11/PA12、USBLC6、CC下拉与实际PCB一致，VCC及VDD33USB/VDD50USB实测约3.3V。
2. 连接数据线后观察USB_Device_IsConfigured、USB_Device_GetLastResult；可在Keil函数返回值、OTG_FS IRQ及Core状态设置断点，不增加固件统计变量。
3. 记录主机VID/PID、18字节设备/配置描述符、SET_ADDRESS和SET_CONFIGURATION；不得将模拟器状态当Windows枚举证据。
4. 验证CRS锁定/误差状态、48MHz实际精度及重插/Reset/Suspend/Resume，覆盖真实芯片修订版本。
5. 本板没有VBUS检测，若由DAP等外部供电，固件不能仅凭本轮接线可靠识别拔线；应按实际供电策略显式Stop。还需核对USB配置前/后电流、500mA声明及多路供电关系。
6. 并行运行相机、SDRAM、IPC、CM4 OLED，观察实时性与USB优先级。当前没有实板操作、没有烧录，不宣称Windows枚举成功。

本阶段完成后不继续开发UVC，下一阶段单独设计描述符与控制状态机。
