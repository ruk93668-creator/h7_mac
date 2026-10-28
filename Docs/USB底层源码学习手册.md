# STM32H747 USB Device 底层源码学习手册

工程：`D:\32\STM32H7\h7_mac`  
阅读基线：`acfddf9`，2026-10-25。本文只解释已经加入的 USB 第一阶段实现，不实现 UVC。

这份手册面向已经懂 C、回调、中断、DMA 和软件分层，但尚不熟悉 USB 的开发者。阅读时先建立通信模型，再顺着真实函数追踪硬件动作。文末提供函数索引和 Keil 调试路线，不设置答题环节。

## 阅读约定与目录

本文明确区分三类内容：

- **协议规则**：来自 USB 2.0 的通信约定，例如方向、标准请求、控制传输阶段。
- **项目实现**：当前工作区实际采用的控制器、函数、描述符、状态和配置。
- **分析与建议**：从代码推导出的使用限制和调试办法，不表示已经完成实板验证。

源码位置按阅读基线标注，后续改代码会造成行号移动，应同时根据函数名定位。代码块中的省略号表示只摘取相关语句；示意流程不代表所有调用都发生在同一个中断或同一时刻。

阅读顺序：

1. [USB 通信模型](#一usb-通信模型)
2. [当前工程的硬件和分层](#二当前工程的硬件和分层)
3. [时钟、电源、GPIO 和 FIFO](#三时钟电源gpio-和-fifo)
4. [从 Initcall 到启动连接](#四从-initcall-到启动连接)
5. [PCD、Core 和回调表](#五pcdcore-和回调表)
6. [端点收发与数据所有权](#六端点收发与数据所有权)
7. [完整控制传输与枚举](#七完整控制传输与枚举)
8. [中断、并发和内存](#八中断并发和内存)
9. [停止、复位和断线](#九停止复位和断线)
10. [Keil 调试路线](#十keil-调试路线)
11. [接口速查与源码索引](#十一接口速查与源码索引)
12. [资料与验证边界](#十二资料与验证边界)

## 一、USB 通信模型

### 1.1 谁安排通信

USB 是 Universal Serial Bus，通用串行总线。在当前 USB 2.0 Device 模式中，电脑是 Host（主机），STM32 是 Device（设备）。正常数据事务由主机发起，设备按安排响应。设备有数据待发，并不表示可以任意抢占总线。

| 接口 | 常见发起方式 | 通信前通常已知的内容 |
|---|---|---|
| UART，通用异步收发器 | 任一发送方可以启动发送 | 波特率、帧格式，应用另约定消息格式 |
| I²C，内部集成电路总线 | 控制器发出 START、地址和时钟 | 设备地址、寄存器协议；总线也支持多控制器机制 |
| SPI，串行外设接口 | 控制器选择片选并产生时钟 | 片选、时钟模式、设备命令 |
| USB | 主机控制器安排总线事务 | 先识别设备，再按描述的接口和端点通信 |

UART/I²C/SPI 上也能构建复杂协议。这张表比较常见使用方式，不是说它们只能传原始字节。USB 自身已经规定设备识别、寻址、数据校验和多种传输规则。

电脑应用程序不需要逐包手工轮询：应用向操作系统提交传输请求，USB 驱动与主机控制器安排实际事务。STM32 同样由硬件执行严格的总线时序，CPU 负责准备数据和处理事件。

### 1.2 Bus、PHY、Controller 各是什么

Bus 是总线，包含连接和通信规则。PHY 是 Physical Layer，物理层收发器，负责把控制器内部信号转换为 D+/D− 上的电气信号，并接收线上的信号。USB Controller 是 USB 控制器，负责端点、包处理、FIFO、状态和中断等硬件动作。

```text
电脑应用 → 操作系统USB驱动 → 主机控制器 → 主机PHY
                                             │ USB线
STM32软件 ← STM32 USB控制器 ← 片内PHY ←─────────┘
```

PHY 不负责识别产品名称，也不解析你写的 C 结构体；这些由设备软件提供。控制器也不会自动知道应用层的视频协议。

### 1.3 Device、Configuration、Interface、Endpoint

Device 是整个设备。Configuration 是一套可选择的设备配置。Interface 是配置内的一项逻辑功能。Endpoint 是设备侧数据通信的逻辑终点。

```text
Device：当前STM32设备
├─ EP0：设备必备的默认控制端点，负责枚举等控制通信
└─ Configuration 1
   └─ Interface 0，Alternate Setting 0
      └─ 当前没有非零端点
```

当前项目有一个配置、一个厂商自定义接口、没有业务数据端点。EP0 不计入接口描述符的 `bNumEndpoints`，所以这里为 0 不等于设备没有 EP0。

一个配置可以有多个接口；接口可以有多个 Alternate Setting（备用设置），用来描述同一接口的不同端点或带宽安排。本项目只有备用设置 0，尚未实现切换。

Endpoint 是逻辑通信对象，不等于某一块普通 SRAM。端点有编号、方向、类型、最大包长和状态；硬件 FIFO 与软件缓冲区为端点工作提供存储。

### 1.4 IN、OUT、SETUP

方向始终从主机角度命名：

| 名称 | 含义 | 当前板上的方向 |
|---|---|---|
| IN | 数据进入主机 | STM32 → 电脑 |
| OUT | 数据离开主机 | 电脑 → STM32 |
| SETUP | 控制传输的建立事务 | 主机发送请求信息给设备 |

端点地址最高位表示方向，低位表示编号：`0x00` 为 EP0 OUT，`0x80` 为 EP0 IN。SETUP 不是第三种普通数据方向，也不是第三根数据线。

### 1.5 Packet、Transaction、Transfer

Packet 是包；Transaction 是一次总线事务；Transfer 是软件请求的一次传输，可能包含多次事务。

```text
Transfer：读取一个设备描述符
├─ SETUP事务：SETUP Token + DATA0请求信息 + ACK
├─ DATA事务：IN Token + DATA1描述符数据 + ACK
└─ STATUS事务：OUT Token + DATA1零长度数据 + ACK
```

Token 是令牌包，说明事务类型以及设备地址、端点。Data 包承载有效数据。Handshake 是握手包，用于报告接收成功、暂时不能响应或请求不可处理等情况。

常见握手：ACK（Acknowledgment，确认）、NAK（Negative Acknowledgment，暂时不能完成）、STALL（端点停止/请求不能处理）。NAK 通常允许主机稍后重试，不应直接当作永久故障。STALL 与 NAK 不同。等时传输没有普通的 ACK/重传机制，不能把上面控制传输的示例套用于全部类型。

DATA0/DATA1 的编号用于数据序列同步，并不是端点编号。控制传输的 SETUP 请求使用 DATA0，随后数据阶段从 DATA1 开始，状态阶段使用 DATA1。多包数据还会涉及翻转和重传，主要由控制器处理。

### 1.6 四种传输类型

| 类型 | 主要目标 | 错误与调度特点 | 常见场景 |
|---|---|---|---|
| Control，控制 | 设备配置和控制请求 | 有标准阶段和错误恢复机制 | EP0枚举 |
| Bulk，批量 | 可靠地传送较多数据 | 可重试，使用可用带宽，不保证延迟 | 文件、批量数据 |
| Interrupt，中断 | 周期性的小量数据 | 主机按调度轮询，不是设备向电脑拉一根中断线 | 键盘、鼠标、状态 |
| Isochronous，等时 | 连续且有时限的数据 | 预留周期带宽，不对出错包作普通重传 | 音视频流 |

本项目目前只开放 Control 类型 EP0。USB 的 Interrupt 传输类型与 Cortex-M 的 IRQ 是两个概念：任何类型的端点都可能引起 MCU 中断。

### 1.7 12 Mbit/s 不等于应用每秒收到 1.5 MB

Full-Speed（FS，全速）的信号速率为 12 Mbit/s。除以 8 得到 1.5 MB/s，仅是未扣开销的换算上限。令牌、校验、握手、包间隔、位填充、调度及其他设备占用都会减少有效吞吐。

High-Speed（HS，高速）为 480 Mbit/s。USB 2.0 不等于设备一定运行在 480 Mbit/s；本板接入的是 FS PHY。USB-C 是连接器形态，也不能直接决定当前数据速率。

FS 的总线 Frame 周期为 1 ms，主机周期发送 SOF。一个 Frame 可以安排多次数据事务，不能据此推断每毫秒只能传一个包。

## 二、当前工程的硬件和分层

### 2.1 当前配置清单

以下是项目实际配置，而不是默认推荐值：

| 项目 | 当前选择 | 依据 |
|---|---|---|
| 核心 | CM7运行USB | Keil CM7 Target包含USB，CM4排除 |
| 控制器 | `USB2_OTG_FS` | BSP实例配置与原理图PA11/PA12 |
| PHY | `PCD_PHY_EMBEDDED` | 片内FS PHY，没有外部ULPI连线证据 |
| 引脚 | PA11 D−、PA12 D+，AF10 | GPIO配置与原理图 |
| 速度 | Full-Speed 12 Mbit/s | `PCD_SPEED_FULL` |
| 内核时钟 | HSI48，CRS参考USB2 SOF | `Usb_ClockInit()` |
| 数据搬运 | PIO，关闭USB内部DMA | `dma_enable = DISABLE` |
| FIFO | Rx 128 Word、Tx0 64 Word | `HAL_PCDEx_Set*FiFo()` |
| 中断优先级 | 6 | 低于现有DCMI/DMA的5 |
| 端点 | EP0 OUT/IN，最大包64字节 | BSP打开端点限制与Core Reset |
| Class | 基础厂商自定义接口 | `s_base_class`及配置描述符 |
| VBUS检测 | 关闭 | VBUS未接入MCU PA9 |

STM32H747的手册可能称两套模块为 OTG_HS1/OTG_HS2；本项目 HAL 使用 `USB2_OTG_FS` 等标识。不要只根据名称中的 HS 推断已连接外部高速 PHY。芯片数据手册说明两套模块均有内部 FS 收发能力，第一套的外部 ULPI 路径才能提供对应 HS 工作方式。[ST STM32H747 数据手册](https://www.st.com/resource/en/datasheet/DM00602212.pdf)

### 2.2 文件各管什么

| 文件 | 应重点读的内容 |
|---|---|
| [bsp_usb.c](D:/32/STM32H7/h7_mac/CM7/BSP/bsp_usb.c) | 时钟、电源、GPIO、PCD、FIFO、NVIC、端点包装、HAL回调 |
| [bsp_usb.h](D:/32/STM32H7/h7_mac/CM7/BSP/bsp_usb.h) | BSP接口和`BspUsbCallbacks_t`事件表 |
| [bsp_usb_hal_config.h](D:/32/STM32H7/h7_mac/CM7/BSP/bsp_usb_hal_config.h) | 通过预包含启用`HAL_PCD_MODULE_ENABLED` |
| [usb_device.c](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c) | 描述符、Class、Core生命周期、USBD_LL适配与BSP事件转发 |
| [usb_device.h](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.h) | 上层使用的USB Device接口 |
| [usbd_conf.h](D:/32/STM32H7/h7_mac/CM7/Platform/usbd_conf.h) | Core容量、日志、固定内存分配映射 |
| [usbd_core.c](D:/32/STM32H7/h7_mac/Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c) | Core初始化、端点阶段处理、设备状态 |
| [usbd_ctlreq.c](D:/32/STM32H7/h7_mac/Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c) | 标准请求解析和处理 |
| [usbd_ioreq.c](D:/32/STM32H7/h7_mac/Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ioreq.c) | EP0数据/状态阶段的启动 |
| [stm32h7xx_hal_pcd.c](D:/32/STM32H7/h7_mac/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c) | HAL硬件上下文、端点启动、中断分发 |
| [stm32h7xx_hal_pcd_ex.c](D:/32/STM32H7/h7_mac/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd_ex.c) | FIFO分配等扩展接口 |
| [stm32h7xx_ll_usb.c](D:/32/STM32H7/h7_mac/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c) | 真正读写USB控制器寄存器和FIFO |
| [README.md](D:/32/STM32H7/h7_mac/Tests/UsbDeviceStage1/README.md) | 移植依据、配置、测试和实板待验 |

```text
Framework Initcall：决定何时初始化和启动
                    │
Platform usb_device.c ↔ ST USB Device Core
                    │ 调用BSP接口 / 接收BSP事件
BSP bsp_usb.c
                    │
HAL PCD / PCDEx
                    │
LL USB → USB控制器寄存器、FIFO → 片内PHY → USB线
```

PCD 是 Peripheral Controller Driver，外设控制器驱动。USBD 是 USB Device，USB设备协议栈。LL 是 Low Layer，底层。本工程里的 `USBD_LL_*` 是 Core 的适配接口，`USB_*` 是 HAL 调用的寄存器驱动函数，两个“LL”层次不能混为一谈。

### 2.3 Keil如何把这些文件编进固件

CM7通过编译预包含 `bsp_usb_hal_config.h` 定义 `HAL_PCD_MODULE_ENABLED`，使HAL配置头展开PCD支持。这样不必直接改CubeMX生成的模块开关位置，但重新生成工程配置时仍需检查预包含选项有没有保留。

Keil工程同时需要编入HAL PCD、PCDEx、LL USB、官方Core三个C文件，以及BSP和Platform。头文件搜索路径提供声明，源文件参与编译提供定义，链接器再把引用连接起来：只有Include Path没有C文件，通常会在链接时出现未定义符号。

CM4排除这些USB文件。检查某个Target是否构建USB，应看它实际的IncludeInBuild、Group设置和构建日志，不能仅因工程树中看到了文件就断定已参与该核编译。

## 三、时钟、电源、GPIO 和 FIFO

### 3.1 两类时钟不能混淆

CPU能通过总线访问USB寄存器，不表示USB PHY已经有正确的48MHz工作时钟。需分别检查外设总线时钟使能与USB内核时钟选择。

[Usb_ClockInit()，第320行](D:/32/STM32H7/h7_mac/CM7/BSP/bsp_usb.c:320) 使用独立HSI48：

```c
osc.OscillatorType = RCC_OSCILLATORTYPE_HSI48;
osc.HSI48State = RCC_HSI48_ON;
osc.PLL.PLLState = RCC_PLL_NONE;
clock.PeriphClockSelection = RCC_PERIPHCLK_USB;
clock.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;
```

HSI48 是48MHz内部高速振荡器；RCC 是 Reset and Clock Control，复位与时钟控制。`RCC_PLL_NONE` 在此次调用中表示不调整PLL，避免改变已工作的系统、相机和SDRAM时钟。

`HAL_RCCEx_GetPeriphCLKFreq(...) == 48000000U` 核对的是HAL按配置计算的名义频率，不是物理测量，不证明温漂、误差和实际精度已经满足要求。

### 3.2 CRS与SOF

CRS 是 Clock Recovery System，时钟恢复系统。SOF 是 Start Of Frame，帧起始。当前CRS以USB2主机SOF为周期参考，对HSI48进行自动修调。

```text
HSI48 → USB内核时钟
   ↑
 CRS ← 主机SOF参考，FS下1kHz
```

配置 `ReloadValue = 48000000 / 1000 - 1 = 47999`。Prescaler 为1，参考边沿为上升沿，误差门限与初始修调采用HAL默认值。

USB需要控制位时间误差，双方才能可靠采样。启动HSI48、CRS收到参考、CRS进入稳定校准状态是不同事件。没有主机SOF时不能声称已锁定。具体电气容差应按USB规范和芯片电气特性检查，不用HAL返回值代替测量。

`s_pcd.Init.Sof_enable = DISABLE` 只是不让PCD向CPU提供SOF软件处理路径，不等于主机停止发送SOF，也不等于CRS不能使用硬件同步参考。

### 3.3 VBUS与USB电源

VBUS 是USB连接器电源线，本板普通USB供电路径为约5V输入经板上电源转换得到3.3V VCC。VDD33USB/VDD50USB是芯片USB供电相关引脚；该板原理图将它们接到VCC，不能仅根据引脚名认定实板VDD50USB一定应加5V。

项目按直接3.3V供电路径执行：

```c
HAL_PWREx_DisableUSBReg();
HAL_PWREx_EnableUSBVoltageDetector();
```

前者关闭内部USB稳压器；后者开启USB供电检测电路。它与 `vbus_sensing_enable` 不是同一开关。

当前MCU未接VBUS检测线，所以关闭VBUS sensing，不把未接线PA9配置成检测输入。原理图说明的是设计连接，仍需测量实际VCC、USB供电脚、供电切换和线材。[ST AN4879 USB硬件指南](https://www.st.com/resource/en/application_note/an4879-introduction-to-usb-hardware-and-pcb-guidelines-using-stm32-mcus-stmicroelectronics.pdf)

### 3.4 GPIO与PHY

[HAL_PCD_MspInit()，第215行](D:/32/STM32H7/h7_mac/CM7/BSP/bsp_usb.c:215) 配置GPIOA时钟、PA11/PA12、AF10、复用推挽、无GPIO上下拉和Very High速度等级，并开启USB2外设时钟。

这里的GPIO速度等级不是USB速率选择。12Mbit/s由PCD速度和PHY决定，不能把GPIO设为Very High就得到480Mbit/s。

HAL Init通过MSP回调取得板级资源。MSP 是 MCU Support Package；它把具体引脚、RCC和NVIC配置放在板级实现中，让通用HAL不必硬编码本板接线。

### 3.5 FIFO的硬件存储

FIFO 是 First In First Out，先进先出。USB控制器内部有专用包存储，软件按需要划分Rx和各Tx FIFO。它不是 `uint8_t buffer[]` 那种任意放置的普通内存数组。

```text
CPU内存                                 USB控制器
描述符/字符串 ─ CPU读内存、写FIFO窗口 ─→ Tx0 FIFO → PHY → 主机
接收缓冲区   ← CPU读FIFO窗口、写内存 ── Rx FIFO  ← PHY ← 主机
```

[HAL_PCDEx_SetRxFiFo()，第112行](D:/32/STM32H7/h7_mac/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd_ex.c:112) 将128写入 `GRXFSIZ`。单位为32位Word，所以Rx容量为512字节。

[HAL_PCDEx_SetTxFiFo()，第70行](D:/32/STM32H7/h7_mac/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd_ex.c:70) 为Tx0设置起始偏移和深度；`DIEPTXF0_HNPTXFSIZ` 中起始位置在Rx之后，深度64 Word即256字节。当前两块共768字节。

FIFO深度256字节和EP0最大包64字节是不同配置：一个是暂存容量，一个是单个数据包有效负载上限。Rx FIFO还承载接收状态等信息，不能把其全部字节都按应用净数据计算。

## 四、从 Initcall 到启动连接

### 4.1 实际顺序

```text
Framework_InitRuntime()
  BUS
    BSP_USB_Init()
      Usb_ClockInit()
      USB电源相关配置
      HAL_PCD_Init()
        HAL_PCD_MspInit()
        USB_CoreInit()
        USB_SetCurrentMode(DEVICE)
        初始化PCD端点上下文
        USB_DevInit()
        USB_DevDisconnect()
      配置Rx与Tx0 FIFO
  DEVICE
    USB_Device_Init()
      USBD_Init()
        USBD_LL_Init()
          BSP_USB_Init()：已完成，直接成功返回
          BSP_USB_SetCallbacks()
      USBD_RegisterClass()
  APPLICATION
    Usb_AutoStart()
      USB_Device_Start()
        USBD_Start()
          USBD_LL_Start()
            BSP_USB_Start()
              HAL_PCD_Start()
```

这条链在启动主循环之前的普通执行上下文运行。裸机中不称为RTOS任务上下文。Initcall按等级执行且失败立即返回；前一初始化失败时，不能假设后续USB启动必然发生。

### 4.2 BSP_USB_Init：准备硬件，不连接主机

入口：[BSP_USB_Init()，第27行](D:/32/STM32H7/h7_mac/CM7/BSP/bsp_usb.c:27)。无参数，返回 `SysResult_t`：成功0，失败为负数。调用方为BUS Initcall，也会由 `USBD_LL_Init()` 再次调用。

它需要解决的问题是：主机发起请求之前，先让时钟、电源、PCD、FIFO具备工作条件。`s_ready` 让重复初始化直接成功返回，第二次不会重新初始化全部硬件。

它先初始化时钟；配置PCD时设置控制器实例、9个端点上下文、FS、内置PHY、禁用DMA/LPM/低功耗/VBUS sensing和独立EP1中断；再调用HAL与FIFO配置。失败时尝试PCD反初始化，只有全部成功才置 `s_ready=true`。

9是当前HAL初始化的端点数量参数，不表示当前Class开放了9条数据通道。实际接口只允许EP0。

若漏掉Init，Start会返回状态错误；若FIFO配置在实际收发后才进行，可能破坏正在处理的数据。Keil先观察函数返回值、`s_ready`、`GRXFSIZ`和Tx0 FIFO配置，不要仅看有没有进入函数。

### 4.3 HAL_PCD_Init：建立硬件驱动上下文

入口：[HAL_PCD_Init()，第123行](D:/32/STM32H7/h7_mac/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:123)。参数 `PCD_HandleTypeDef *hpcd` 是HAL硬件上下文，返回HAL状态。初始静态句柄处于RESET状态时，HAL调用MSP准备板级资源；随后状态先进入BUSY，完成核心、模式和端点初始化后变为READY。

PCD READY仅表示驱动初始化完成，不表示主机已识别设备。函数尾部调用 `USB_DevDisconnect()`，保持软件断开状态。此时读到READY而电脑没有出现新设备，并不矛盾。

LL设置PHY和Device模式、相关中断掩码等寄存器。寄存器归属应看本版本 [stm32h7xx_ll_usb.c](D:/32/STM32H7/h7_mac/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c) 中的 `USB_CoreInit/USB_DevInit`，不要把另一个STM32型号的初始化值直接复制过来。

### 4.4 BSP_USB_SetCallbacks：先建立上行通路

入口：[BSP_USB_SetCallbacks()，第79行](D:/32/STM32H7/h7_mac/CM7/BSP/bsp_usb.c:79)。参数是静态事件表指针；setup、reset、data_in和data_out必须非空。返回参数错误或状态错误时，不会完成绑定。

它只保存指针，不复制整张表，调用者不能传一个函数返回后失效的栈上表。当前Platform传入 `static const BspUsbCallbacks_t s_callbacks`，生命周期覆盖USB运行期。

绑定只能在BSP已初始化且尚未启动时进行。否则启动后IRQ可能访问未准备好的函数指针，或在更新表时看到不一致对象。

### 4.5 USB_Device_Init：准备协议栈

入口：[USB_Device_Init()，第159行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:159)。无参数，返回工程错误码。它清除本次底层错误记录，调用 `USBD_Init(&s_device, &s_descriptors, 0U)`，然后注册基础Class。只有两步成功才置 `s_initialized=true`。

`USBD_Init()` 绑定描述符表、设置初始设备状态并调用 `USBD_LL_Init()`；它不会自动实现你的视频协议。`USBD_RegisterClass()` 绑定Class操作表，也不等于已经执行Class Init；后者在主机选择配置时发生。

### 4.6 BSP_USB_Start：对外连接

入口：[BSP_USB_Start()，第92行](D:/32/STM32H7/h7_mac/CM7/BSP/bsp_usb.c:92)。无参数，未初始化/未绑定事件表返回状态错误，重复启动返回BUSY，HAL失败转换为工程错误码。

```text
清除NVIC旧Pending
    ↓
HAL_PCD_Start：开放控制器全局中断，执行软件连接
    ↓
s_started = true
    ↓
开放NVIC OTG_FS_IRQn
```

[USB_DevConnect()，第1168行](D:/32/STM32H7/h7_mac/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:1168) 清除 `DCTL.SDIS`（Soft Disconnect，软件断开）位。软件连接让控制器进入可被主机识别的连接状态；不是由这一步执行全部枚举。

控制器事件可能在NVIC尚未开放时挂起。当前先提交BSP启动状态，再允许CPU进入USB中断，避免IRQ入口因 `s_started=false` 而忽略已启动硬件。

Platform的 `USB_Device_Start()` 还会先清除旧 `dev_config` 和旧CONFIGURED状态，成功后才提交自己的 `s_started`。BSP和Platform各有一个同名私有变量，分别表示各层生命周期，不是同一个对象。

### 4.7 PCD配置逐项理解

| 字段 | 当前值 | 它改变的选择 | 它不代表什么 |
|---|---|---|---|
| Instance | USB2_OTG_FS | 选择第二套USB控制器 | 不是任意USB-C接口 |
| dev_endpoints | 9 | HAL初始化端点上下文数量 | 不是当前开放9个业务端点 |
| speed | FULL | FS工作速度 | 不是GPIO速度等级 |
| dma_enable | DISABLE | 使用CPU搬运FIFO数据 | 不是关闭DCMI的DMA |
| phy_itface | EMBEDDED | 使用片内PHY | 不要求外部ULPI芯片 |
| Sof_enable | DISABLE | 不启用PCD SOF软件处理 | 不禁止主机SOF或CRS同步 |
| low_power_enable | DISABLE | 不启用该低功耗处理策略 | 不禁止总线出现Suspend |
| lpm_enable | DISABLE | 不启用LPM扩展处理 | 与普通Suspend不是同一设置 |
| vbus_sensing_enable | DISABLE | 不使用未接线的硬件VBUS检测 | 不会凭空获得拔线检测能力 |
| use_dedicated_ep1 | DISABLE | 不用独立EP1中断路径 | 不决定将来能否设计EP1业务 |

LPM是Link Power Management，链路电源管理。本阶段只学习这些选项为何这样选，不能把修改一个字段视作已经实现对应功能。

## 五、PCD、Core 和回调表

### 5.1 两个句柄不是同一种状态

| 对象 | 保存的内容 | 不负责的内容 |
|---|---|---|
| `s_pcd`，`PCD_HandleTypeDef` | USB实例、Init参数、IN/OUT端点缓冲指针、长度、计数、HAL状态、SETUP原始数据 | 产品身份、接口协议、完整枚举语义 |
| `s_device`，`USBD_HandleTypeDef` | 设备地址、配置、Default/Addressed/Configured状态、EP0阶段、描述符和Class指针 | 本板GPIO和独立HSI48配置 |

HAL PCD READY、Core CONFIGURED、Platform `s_started` 是三种不同状态。一个布尔变量无法替代全部生命周期。

### 5.2 为什么需要USBD_LL

Core知道“现在需要发送18字节设备描述符”，但不应硬编码PA11/PA12或控制器句柄。`USBD_LL_Transmit()` 是Core向具体硬件请求发送的适配入口。

```text
Core（标准协议逻辑）
   ↓ 调用约定好的USBD_LL接口
Platform（检查设备实例、转换返回值）
   ↓ BSP_USB_Transmit等接口
BSP（私有PCD句柄）
   ↓ HAL/LL
寄存器和FIFO
```

当前Platform的LL接口检查 `device == &s_device`，避免其他实例误操作本模块硬件。BSP返回 `SysResult_t`，`Usb_LlResult()` 转为 `USBD_OK/USBD_BUSY/USBD_FAIL`，并锁存负数错误原因。

### 5.3 硬件事件怎样反向进入Core

```text
HAL_PCD_SetupStageCallback()
    ↓ s_callbacks->setup(...)
Usb_Setup()
    ↓
USBD_LL_SetupStage()
```

BSP只依赖自己头文件定义的回调契约，不需要包含Core头文件。Platform负责把自己的函数地址装入表，再交给BSP。底层调用的是约定好的回调，上层决定回调背后执行哪个协议栈，这就是本工程的依赖倒置方式。

当前HAL配置 `USE_HAL_PCD_REGISTER_CALLBACKS = 0U`，因此工程通过同名强定义替代HAL弱回调，并非运行时使用 `HAL_PCD_RegisterCallback()` 注册。BSP回调表是本工程另外一层设计，不要与HAL可选注册机制混为一谈。

### 5.4 Core和Class的边界

Core负责通用USB设备状态、请求分发和EP0阶段。Class负责某类设备特有的描述、控制请求和端点业务。

当前 `s_base_class` 仅用于基础枚举：Init接受配置1；DeInit没有额外资源可释放；Setup支持接口0的GET_INTERFACE与GET_STATUS，其余请求STALL。没有非零端点、视频控制或视频传输。

`USB_Device_RegisterClass(driver, descriptors)` 允许在已初始化、未启动时换成别的静态Class表和描述符表。它检查必要函数指针及FS配置头，清零 `NumClasses` 后注册并替换 `pDesc`。这不是完整描述符验证器，也不表示多Class复合设备已经实现。

### 5.5 HAL回调逐个怎样看

这些回调正常由HAL中断处理调用，返回值均为void。`pcd` 指明发生事件的硬件句柄；当前BSP先要求它等于私有 `&s_pcd`，避免其他控制器事件误入本设备。端点回调的 `ep` 为端点编号，不要直接当作包含方向位的完整地址。

| HAL回调 | 额外参数 | BSP传给Platform的内容 | 若转发缺失，主要影响 |
|---|---|---|---|
| SetupStageCallback | 无 | PCD中已读取的8字节SETUP地址 | Core无法解析主机请求 |
| DataInStageCallback | ep编号 | ep、当前IN端点xfer_buff | Core无法正常推进IN完成后的阶段 |
| DataOutStageCallback | ep编号 | ep、当前OUT端点xfer_buff | Core无法处理OUT阶段完成 |
| ResetCallback | 无 | 无参数Reset事件 | Core状态和EP0可能未正确准备 |
| SuspendCallback | 无 | 可选Suspend事件 | Core未及时记录挂起状态 |
| ResumeCallback | 无 | 可选Resume事件 | Core不能按这条路径恢复状态 |
| ConnectCallback | 无 | 可选Connect事件 | 丢失该硬件事件通知，不应推导所有接线都能检测插线 |
| DisconnectCallback | 无 | 可选Disconnect事件 | 丢失通知；本板缺VBUS检测时该事件本身也未必可靠产生 |

这些函数主要改变上层协议状态或发起下一阶段端点操作，不负责把每个硬件中断标志重新清一遍。标志的读取、清除和FIFO处理已经由HAL相应分支完成；不熟悉清除规则时，不能在回调里追加任意清标志代码。

Keil跟踪回调时看三件事：`pcd` 是否指向本实例、BSP表中函数地址是否正确、进入Platform后Core的哪个状态发生变化。只看到回调命中，尚不能证明整个控制传输成功。

## 六、端点收发与数据所有权

### 6.1 BSP_USB_OpenEp

入口：[BSP_USB_OpenEp()，第142行](D:/32/STM32H7/h7_mac/CM7/BSP/bsp_usb.c:142)。参数为端点地址 `ep`、类型 `type` 和最大包长 `size`。当前仅接受0x00/0x80、控制类型、64字节；其他组合返回NOT_SUPPORTED。

它由总线Reset后的Core路径调用，最终 `HAL_PCD_EP_Open()` 设置端点上下文并通过LL激活端点寄存器。没有打开EP0，设备就不能正常进行基本控制通信。

不要把“Init里有9个端点上下文”理解为可以绕过BSP限制打开EP1。业务端点要同时考虑描述符、Class操作、FIFO、驱动和数据生命周期。

### 6.2 BSP_USB_Transmit与HAL发送

入口：[BSP_USB_Transmit()，第193行](D:/32/STM32H7/h7_mac/CM7/BSP/bsp_usb.c:193)。参数是端点、缓冲区地址和字节数，返回工程错误码。NULL且长度非零是错误，NULL且长度为零可用于零长度状态包。

```text
USBD_CtlSendData()
  → USBD_LL_Transmit()
  → BSP_USB_Transmit()
  → HAL_PCD_EP_Transmit()
  → USB_EPStartXfer()
```

[HAL_PCD_EP_Transmit()，第1884行](D:/32/STM32H7/h7_mac/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1884) 保存 `xfer_buff/xfer_len`、清零 `xfer_count`，标记IN方向，再让LL配置传输。它没有等待电脑收完数据，因此返回HAL_OK不等于主机已收到。

当前PIO控制端点在LL里设置传输长度/包数、`DIEPCTL.EPENA`和`CNAK`，并开启相应 `DIEPEMPMSK` 位。之后FIFO空中断调用 `PCD_WriteEmptyTxFifo()`，再由 `USB_WritePacket()` 把内存读到FIFO。

关键细节：本版本EP0控制发送不是在调用 `HAL_PCD_EP_Transmit()` 的瞬间把所有数据同步复制完。因此不能在函数返回后立即释放栈缓冲区或改写数据。

### 6.3 xfer_count不是主机应用接收计数

PIO发送时，`PCD_WriteEmptyTxFifo()` 在CPU把数据写入FIFO后增加 `xfer_count`，并推进 `xfer_buff`。这些数据可能还在FIFO中，所以 `xfer_count == xfer_len` 不等于主机应用已经处理数据。

硬件传输完成有自己的XFRC事件；控制传输还有后续STATUS阶段。要判断完成，必须明确问的是“搬到FIFO”“一个IN数据阶段完成”“整个控制传输完成”还是“主机应用消费完成”。

### 6.4 BSP_USB_Receive与HAL接收

入口：[BSP_USB_Receive()，第200行](D:/32/STM32H7/h7_mac/CM7/BSP/bsp_usb.c:200)。当前只接受EP0 OUT，缓冲区在接收结束前必须可写且有效。

`HAL_PCD_EP_Receive()` 保存OUT缓冲地址和长度，调用 `USB_EPStartXfer()` 配置OUT传输大小及端点。主机发送后，Rx FIFO事件由HAL读取数据并写入内存。`HAL_PCD_DataOutStageCallback()` 再通知Core推进状态。

EP0底层可能按最大包长安排接收，Core用请求长度和阶段信息管理有效数据，不能只看LL的单个长度字段就断定控制请求语义。

### 6.5 内存布局和生命周期

```text
普通RW/ZI区域（当前Scatter放在DTCM）
├─ s_pcd：硬件驱动状态
├─ s_device：协议状态
├─ s_device_desc[20]：有效18字节，尾部补齐
├─ s_config_desc[20]：有效18字节，尾部补齐
├─ s_string_desc[64]：当前字符串请求共用缓冲区
└─ s_class_memory[1024]：唯一固定分配块
```

`USB_WritePacket()` 按 `(len+3)/4` 次32位读取。18字节发送会读5个Word，因此描述符数组预留20字节，避免最后一次CPU读取越出对象。USB实际发送长度仍为18，补齐不代表多发2字节。

固定Class池只允许一个未释放块；长度0、超过1024或已占用时返回NULL。`USB_Device_Free()` 只识别该块起始地址。没有malloc，也不是可分割的通用内存池。分配操作没有通用并发保护，未来使用时需安排调用上下文，不能自动视为线程/中断安全分配器。

## 七、完整控制传输与枚举

### 7.1 设备状态与EP0阶段是两套状态机

```text
设备状态：
总线Reset → DEFAULT（地址0）
                │ SET_ADDRESS，非零地址
                ▼
            ADDRESSED
                │ SET_CONFIGURATION(1)，Class Init成功
                ▼
            CONFIGURED
                │ SET_CONFIGURATION(0)
                └────────────→ ADDRESSED
```

Suspend（挂起）是另一生命周期情况，Core还会保存恢复前状态。不要把它等同于Stop或永久断开。

```text
EP0单次控制读取：
SETUP → DATA_IN → STATUS_OUT → 等待后续请求

EP0无数据控制命令：
SETUP → STATUS_IN → 等待后续请求
```

设备可以一直处于CONFIGURED，同时不断执行新的EP0控制请求。

### 7.2 先纠正ENUMDNE的含义

本版本HAL有两个不同分支：

1. USBRST：清理端点/STALL等状态，设备地址清0，准备接收SETUP。
2. ENUMDNE：读取控制器识别的速度、配置周转时间，调用 `HAL_PCD_ResetCallback()`。

实际Reset回调位于 [stm32h7xx_hal_pcd.c](D:/32/STM32H7/h7_mac/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c) 的ENUMDNE分支。名字中的“Enumeration done”容易误导：这里不能当作电脑已读取全部描述符并发送SET_CONFIGURATION的证明。

```text
USBRST分支准备硬件
   ↓ 控制器完成速度识别
ENUMDNE分支
   → HAL_PCD_ResetCallback()
   → BSP事件表.reset
   → Usb_Reset()
   → USBD_LL_SetSpeed(FULL)
   → USBD_LL_Reset()
   → USBD_LL_OpenEP(0x00/0x80)
```

Core Reset还会复位配置、EP0状态，并按实现调用Class DeInit。此后主机才继续标准控制请求。

### 7.3 用真实描述符请求贯穿全部层次

下面选择本工程能够处理的一次请求作为教学例子，不声称它是已捕获的实板数据包。主机请求18字节设备描述符，其SETUP内容可以是：

```text
80 06 00 01 00 00 12 00
```

| 字节 | 字段 | 含义 |
|---|---|---|
| 80 | bmRequestType | 设备到主机、标准请求、接收对象为设备 |
| 06 | bRequest | GET_DESCRIPTOR |
| 00 01 | wValue | 小端0x0100：设备描述符类型1、索引0 |
| 00 00 | wIndex | 本次设备描述符查询为0 |
| 12 00 | wLength | 主机最多接收18字节 |

这8字节是SETUP事务DATA0包里的请求内容，不包含Token、PID、CRC或握手字节。请求类型字节0x80也不是“端点地址字段”，即使数值恰好与EP0 IN地址一致。

### 7.4 SETUP阶段：从线到Core

```text
主机：SETUP Token + DATA0(8字节请求)
        ↓
片内PHY与USB控制器接收
        ↓ Rx FIFO状态/端点SETUP中断
OTG_FS_IRQHandler()
        ↓
HAL_PCD_IRQHandler()
  RXFLVL路径：读GRXSTSP，识别STS_SETUP_UPDT
  USB_ReadPacket(..., hpcd->Setup, 8)
  OUT端点STUP路径：PCD_EP_OutSetupPacket_int()
        ↓
HAL_PCD_SetupStageCallback(&s_pcd)
        ↓ s_callbacks->setup((uint8_t *)pcd->Setup)
Usb_Setup(data)
        ↓
USBD_LL_SetupStage(&s_device, data)
```

Rx数据读取和SETUP完成通知是两个相关动作，可能分布在不同硬件事件处理时刻，不要理解为仅进入IRQ一次就必然执行整张图的全部步骤。

Core先调用 `USBD_ParseSetupRequest()` 解析小端字段，然后设置 `ep0_state=USBD_EP0_SETUP`、`ep0_data_len=wLength`。根据接收对象分发到设备/接口/端点请求处理函数；本例进入 `USBD_StdDevReq()`。

### 7.5 DATA阶段：从描述符到FIFO

```text
USBD_StdDevReq()
  → USBD_GetDescriptor()
  → pDesc->GetDeviceDescriptor()
  → Usb_DeviceDesc()：返回s_device_desc，length=18
  → MIN(描述符长度, 主机wLength)
  → USBD_CtlSendData(..., 18)
       ep0_state = DATA_IN
       total_length = rem_length = 18
  → USBD_LL_Transmit(..., ep=0x00, ...)
  → BSP_USB_Transmit()
  → HAL_PCD_EP_Transmit()
  → USB_EPStartXfer()
       设置IN端点长度、包数、使能和FIFO空中断
```

注意官方Core调用发送接口时传入的是端点编号0x00，HAL发送函数本身选择 `IN_ep[]` 并设置IN方向，所以当前BSP发送包装允许0x00和0x80。这不改变USB总线上IN方向的定义。

之后Tx FIFO空事件触发HAL的 `PCD_WriteEmptyTxFifo()`，CPU通过 `USB_WritePacket()` 把描述符写入Tx0 FIFO。主机发出对应IN Token时，控制器按USB时序发送描述符，主机接收并ACK。

本工程的有效设备描述符是：

```text
12 01 00 02 00 00 00 40 09 12 01 00 00 01 01 02 03 01
```

其中 `09 12` 是小端VID 0x1209，`01 00` 是PID 0x0001；0x40为EP0最大包64；字符串索引依次1、2、3；最后1表示一个配置。不要将PID与数据包标识Packet Identifier混淆，这里PID是Product ID。

### 7.6 STATUS阶段与完成中断

IN阶段完成后，HAL识别IN端点XFRC，清除相应中断状态并调用DataIn回调：

```text
HAL_PCD_DataInStageCallback()
  → s_callbacks->data_in
  → Usb_DataIn()
  → USBD_LL_DataInStage()
  → 检查EP0剩余长度，必要时继续发送下一包
  → 本例18字节已发完
  → USBD_CtlReceiveStatus()
       ep0_state = STATUS_OUT
  → USBD_LL_PrepareReceive(..., NULL, 0)
  → BSP_USB_Receive()
  → HAL_PCD_EP_Receive()
```

主机发送OUT Token及零长度DATA1，设备ACK；随后OUT完成路径通过 `HAL_PCD_DataOutStageCallback → Usb_DataOut → USBD_LL_DataOutStage` 处理状态阶段。

当前Core在最后DATA_IN后还会调用 `USBD_LL_StallEP(..., 0x80)` 再准备STATUS_OUT。这是该版本源码的EP0处理步骤，不能仅凭这一次内部设置就判断整个GET_DESCRIPTOR请求失败；应结合总线包、下一请求和完整Core路径分析。

Zero-Length Packet（ZLP，零长度包）不是“没有任何USB活动”。它仍有数据包协议开销，可承载状态阶段，或在某些数据传输条件下标记结束。

### 7.7 更长的数据和短包

主机 `wLength=8` 时，本项目只返回设备描述符前8字节。主机请求64字节而描述符只有18字节时，Core按18字节发送，短于最大包的包可结束该数据阶段。

若实际数据长度恰好为最大包长整数倍，又少于主机请求的长度，可能需要额外ZLP表示数据结束。当前Core在 `USBD_LL_DataInStage()` 检查总长、剩余长度和主机请求长度后处理，不能无条件给每次发送都补一个ZLP。

### 7.8 SET_ADDRESS

示例SETUP：`00 05 05 00 00 00 00 00`，请求设备地址5，无DATA阶段。

[USBD_SetAddress()，第682行](D:/32/STM32H7/h7_mac/Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c:682) 检查索引、长度、地址范围和设备状态后，记录 `dev_address`，调用 `USBD_LL_SetUSBAddress → BSP_USB_SetAddress → HAL_PCD_SetAddress → USB_SetDevAddress`，最后准备STATUS_IN并更新Core状态。

LL将地址写到 `DCFG.DAD` 字段。USB规则要求地址切换遵守SET_ADDRESS状态阶段时序；源码写寄存器的位置与总线上何时使用新地址不能简单画等号。当前实现沿用ST HAL与控制器机制，不额外手写“等一个延时再改地址”的替代逻辑。实板应从控制传输及后续地址5的事务核对。

### 7.9 SET_CONFIGURATION

示例SETUP：`00 09 01 00 00 00 00 00`，选择配置1，无DATA阶段。

[USBD_SetConfig()，第723行](D:/32/STM32H7/h7_mac/Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c:723) 在ADDRESSED状态保存配置值，经 `USBD_SetClassConfig()` 调用基础Class Init。成功后启动STATUS_IN并将Core状态设为CONFIGURED；失败则STALL并保持/退回相应状态。

Core在安排状态包时就更新软件状态，所以观察到CONFIGURED不等于电脑应用驱动已完成绑定，也不等于主机已经消费了该状态包。`USB_Device_IsConfigured()` 是软件侧观察接口，不能替代主机枚举证据。

主机可以先读前8字节、Reset、设地址、再读完整描述符及配置/字符串；具体请求次序和重复次数随主机实现变化。教学顺序不应写成所有电脑必须逐字照做的固定脚本。

## 八、中断、并发和内存

### 8.1 三层中断门控

```text
端点/总线事件
   ↓ 对应端点掩码、GINTMSK等
控制器全局中断门控：GAHBCFG.GINT
   ↓
NVIC OTG_FS_IRQn
   ↓ CPU屏蔽状态和优先级允许
OTG_FS_IRQHandler()
```

NVIC是Nested Vectored Interrupt Controller，嵌套向量中断控制器。只开放NVIC不能代替控制器中断配置；只看到控制器标志也不能证明CPU已进入IRQ。

本工程IRQ入口要求BSP `s_ready && s_started` 才调用HAL。HAL再检查Device模式和有效中断状态，逐项处理Rx、OUT、IN、Reset、速度识别、Suspend/Resume等事件。

### 8.2 常见寄存器怎么读

| 寄存器/字段 | 学习时要关注什么 | 读取注意 |
|---|---|---|
| GUSBCFG | PHY选择、模式相关配置 | 不能只靠变量名判断当前物理链路 |
| GAHBCFG.GINT | 控制器全局中断允许 | 与NVIC不同 |
| GINTSTS/GINTMSK | 已发生事件及允许上报事件 | 部分标志为写1清除或有专门清除步骤 |
| GRXFSIZ | Rx FIFO Word深度 | 当前128 |
| DIEPTXF0_HNPTXFSIZ | Tx0起始位置/Word深度 | 当前起点在Rx之后，深度64 |
| GRXSTSP | 弹出接收状态信息 | **读操作有副作用，不要放在自动刷新的Watch里** |
| DCFG.DAD | USB设备地址 | Reset回0，SET_ADDRESS后变化 |
| DCTL.SDIS | 软件断开控制 | Connect清零，Disconnect置位 |
| DAINT/DAINTMSK | IN/OUT端点中断汇总/掩码 | 配合具体端点中断读 |
| DIEPINT/DOEPINT | 端点发送/接收、SETUP等事件 | 清除规则看HAL与RM |
| DIEPTSIZ/DOEPTSIZ | 传输大小和包数 | 区分单包底层安排和Core总长度 |
| DIEPEMPMSK/DTXFSTS | FIFO空中断选择/可用Word数 | 用于理解PIO装填 |
| FIFO数据窗口 | 实际出入FIFO的数据 | **读取会取走数据，不要随意查看** |

寄存器名称和读写动作已与项目HAL/LL核对。完整位定义、访问副作用和芯片修订差异应查RM0399对应USB章节；本文不把任意寄存器都当作普通可反复读取的内存。

### 8.3 回调在哪个上下文运行

当前正常USB事件链：IRQ → HAL → BSP回调 → Platform → Core，都是同步函数调用，因此仍处于同一个USB中断上下文。函数叫 `Usb_Setup` 并不会让它自动切换到主循环。

描述符查询、Class Setup、主机配置触发的Class Init/DeInit，也可能在这条链中执行。因此不要在这些回调里等待DMA、做长时间循环、访问阻塞设备或执行依赖低优先级SysTick的长延时。

`USBD_LL_Delay()` 的HAL_Delay包装是提供给Core的接口，不代表可以在任意Class回调中安全调用延时。

### 8.4 USB与DCMI并发

当前USB优先级为6，DCMI/DMA为5，SysTick为15。是否可抢占还需结合实际NVIC分组；按项目既有配置意图，DCMI/DMA比USB优先。

USB ISR可能被DCMI/DMA打断，DCMI处理过久会推迟USB服务；反过来，USB长时间占CPU也会影响主循环。优先级只决定抢占顺序，不会让带宽、FIFO容量或CPU时间无限增加。

本阶段没有把图像缓冲区交给USB。未来要传图像，必须明确“谁持有帧、USB何时真正用完、何时允许DMA覆盖”，不能在发送接口返回时就把摄像头帧释放。这里只解释边界，不实现新的状态机。

### 8.5 volatile与一致性

`s_callbacks`、BSP启动标志和 `s_last_result` 使用volatile，是因为可能跨普通代码和ISR观察。volatile使相关访问按语义发生，但不是互斥锁，也不会自动让多字段状态修改形成原子事务。

当前通过初始化时绑定、运行时不替换回调、Start/Stop安排IRQ门控来约束访问。若以后增加运行时重配置或RTOS多任务调用，必须重新审核这些约束。

### 8.6 DTCM、Cache与PIO

DTCM是Data Tightly Coupled Memory，数据紧耦合内存。本项目Scatter将普通RW/ZI放到0x20000000起始DTCM，因此USB上下文和静态缓冲区也在那里。

当前USB内部DMA关闭，CPU读取DTCM中的描述符，然后写USB外设FIFO；USB控制器不会直接访问这些DTCM地址。因此当前不因USB收发增加D-Cache维护。

这不意味着所有DMA都能访问DTCM。若以后更换控制器/PHY或开启DMA，需重新核对总线可达性、缓冲区位置、对齐、MPU属性和Cache清理/失效。仅把 `dma_enable` 改成ENABLE不能完成移植。当前也不能因为关闭DMA就忽略缓冲区有效期。

## 九、停止、复位和断线

### 9.1 Stop不是DeInit

[BSP_USB_Stop()，第110行](D:/32/STM32H7/h7_mac/CM7/BSP/bsp_usb.c:110) 先关闭NVIC，再调用HAL Stop，清NVIC Pending；只有HAL成功才将BSP启动标志清零。HAL Stop关闭控制器全局中断、软件断开并冲刷Tx FIFO。Stop保留初始化资源，允许后续Start。

[BSP_USB_DeInit()，第124行](D:/32/STM32H7/h7_mac/CM7/BSP/bsp_usb.c:124) 先Stop，成功后才HAL DeInit；由MSP DeInit关闭USB外设时钟、禁用IRQ并释放PA11/PA12，最后清除回调表和ready状态。当前实现不会顺手关闭其他模块可能使用的GPIOA时钟，也不会把所有共享时钟源一并关闭。

### 9.2 Platform为什么单独保存错误

当前官方 `USBD_Stop()` 路径并不保证把每一个LL失败原样返回。Platform先清 `s_last_result`，让LL转换函数记录BSP失败，随后同时检查Core返回值与保存的底层结果，避免“硬件停止失败但上层报告成功”。

`USB_Device_GetLastResult()` 读取的是最近锁存的适配失败原因，不是完整历史、不是所有USB错误统计，也不表示每次GET_DESCRIPTOR成功后都会自动清零。`Usb_LlResult()` 成功时直接返回，不清历史错误；Start/Stop/Init会在相应入口重置本次记录。

### 9.3 Reset、Suspend、Disconnect的区别

| 事件 | 含义 | 当前处理方式 |
|---|---|---|
| 总线Reset | 主机重新建立设备通信状态 | HAL清理硬件、Core复位状态并打开EP0 |
| Suspend | 总线进入挂起 | 事件转发Core，当前不启用PCD低功耗策略 |
| Resume | 从挂起恢复 | 转发Core恢复状态 |
| 软件Stop | 固件主动断开 | BSP/HAL执行软件断开并禁用中断 |
| DeInit | 释放硬件资源 | Stop后关闭外设和引脚 |
| 实际拔线 | 物理连接消失 | 本板没有VBUS检测，不能保证单凭当前回调可靠识别 |

Reset不会重跑整个Framework；也不是CPU复位。Connect回调的名字不保证任何供电条件下都能检测到插线。若板子仍由DAP供电，USB线拔掉但MCU继续运行，软件可能保留旧状态，必须结合实际硬件和供电策略验收。

## 十、Keil 调试路线

### 10.1 先确认你在调正确的设备

调试CM7 Target；插接本板PA11/PA12对应USB口，而不是仅连接调试器自身的USB口。USB线必须支持数据。用万用表确认供电，检查连接器、CC下拉和ESD器件；原理图不能证明焊接正确。

当前VID/PID为内部测试身份1209:0001，不唯一。主机看到的应是基础厂商自定义设备，尚不是串口或摄像头。即使枚举成功，Windows没有匹配功能驱动，也不能据此断言EP0失败。

### 10.2 分阶段看状态

| 顺序 | 断点/观察位置 | 预期软件现象 | 说明 |
|---|---|---|---|
| 1 | BSP_USB_Init返回 | SYS_OK、s_ready=true | 硬件准备完成 |
| 2 | USB_Device_Init返回 | SYS_OK、s_initialized=true | Core和Class绑定完成 |
| 3 | BSP_USB_Start返回 | BSP s_started=true | NVIC已开放，控制器已连接 |
| 4 | HAL_PCD_IRQHandler | 能看到Reset/速度识别等事件 | 主机/硬件开始交互 |
| 5 | HAL_PCD_ResetCallback | 进入Core Reset，EP0打开 | 不是全部枚举完成 |
| 6 | USBD_LL_SetupStage | request各字段有效 | 主机开始标准控制请求 |
| 7 | Usb_DeviceDesc/USBD_CtlSendData | 指针有效、len符合请求 | 描述符已安排发送 |
| 8 | HAL_PCD_DataInStageCallback | 进入Core阶段推进 | 检查数据/状态阶段 |
| 9 | USBD_SetAddress | 地址改变，Core ADDRESSED | 主机分配地址 |
| 10 | USBD_SetConfig | Class Init成功、Core CONFIGURED | 软件已处理配置请求 |

可在对应文件作用域观察 `s_pcd`、`s_device`、`s_last_result`。私有static同名变量需要让Keil明确定位所属文件，不能把BSP的 `s_started` 与Platform的同名变量混看。

主机请求的关键观察字段：`s_device.request.bmRequest`、`bRequest`、`wValue`、`wIndex`、`wLength`；以及 `dev_state`、`dev_address`、`dev_config`、`ep0_state`、`ep0_data_len`、`ep_in[0].rem_length`。

### 10.3 断点会改变USB时序

主机不会因为CM7停在断点就无限等待。长时间暂停可能触发超时、重试、Reset或重新枚举。调试建议先用少量断点确认路径，再取消断点正常运行，用主机USB设备树/USBView、USBPcap等工具观察描述符和请求。抓包工具记录的是实际通信，不能用模拟SETUP示例代替抓包证据。

使用USBView看身份和描述符；需要更细请求顺序时用主机抓包。USBPcap通常呈现主机传输请求/完成信息，不等同于能看到全部电气级Token和ACK；要检查线级时序与信号质量，需硬件协议分析仪或合适的示波器测量。

### 10.4 按现象缩小范围

| 现象 | 优先检查 |
|---|---|
| Init直接失败 | Initcall前序结果、HSI48就绪、USB时钟选择、HAL返回值 |
| 电脑完全没有连接反应 | 实际USB口/数据线、供电、PA11/12、软件Start是否执行、SDIS |
| 有控制器标志但无IRQ | GINTMSK、GAHBCFG.GINT、NVIC、PRIMASK、向量名称、BSP启动标志 |
| 有Reset没有SETUP | 时钟精度、EP0准备、请求是否因断点超时、硬件连接 |
| 有SETUP但不发描述符 | 请求解析、描述符表绑定、类型/长度、端点配置、FIFO空中断 |
| xfer_count增加但电脑报超时 | 计数可能仅代表写入FIFO，继续看XFRC、状态阶段及主机抓包 |
| CONFIGURED但无COM口/摄像头 | 当前Class本来不提供这些功能 |
| 拔线后仍显示旧状态 | 检查供电与缺失VBUS检测，不把软件旧状态当物理连接证明 |

不要用反复读取 `GRXSTSP` 或FIFO数据窗口来调试，它们有取走数据/状态的副作用。普通Memory/Watch窗口自动刷新可能自己制造问题。

## 十一、接口速查与源码索引

### 11.1 BSP接口

所有BSP操作使用私有PCD句柄，不向Platform公开HAL对象。初始化、回调绑定、Start/Stop/DeInit应由受控的普通执行路径安排，不把这些接口当作可随意在ISR重入的线程安全API。

| 接口 | 参数与结果 | 调用关系、作用与调试重点 |
|---|---|---|
| BSP_USB_Init | 无参，SysResult_t | BUS或LL Init→时钟/PCD/FIFO；看ready与HAL失败 |
| BSP_USB_SetCallbacks | 静态表指针，SysResult_t | Platform绑定→保存表指针；只允许停止时修改 |
| BSP_USB_Start | 无参，SysResult_t | LL Start→HAL Start→Connect；看SDIS、NVIC |
| BSP_USB_Stop | 无参，SysResult_t | LL Stop→禁IRQ/HAL Stop；失败不伪装已停止 |
| BSP_USB_DeInit | 无参，SysResult_t | LL DeInit→Stop/HAL/MSP释放；看ready清除 |
| BSP_USB_OpenEp | 地址/类型/最大包长，SysResult_t | Core Reset→HAL EP Open；只支持EP0控制64 |
| BSP_USB_CloseEp | 地址，SysResult_t | LL Close→HAL EP Close；关闭指定方向 |
| BSP_USB_FlushEp | 地址，SysResult_t | LL Flush→HAL EP Flush；清FIFO，不代表数据已送达 |
| BSP_USB_StallEp | 地址，SysResult_t | Core拒绝请求→HAL SetStall；看端点STALL |
| BSP_USB_ClearStallEp | 地址，SysResult_t | Core请求→HAL ClrStall；解除端点停止 |
| BSP_USB_IsStallEp | 地址，bool | LL查询→PCD IN/OUT端点is_stall |
| BSP_USB_SetAddress | 0~127地址，SysResult_t | Core请求→HAL/LL→DCFG.DAD |
| BSP_USB_Transmit | 端点/指针/字节数，SysResult_t | Core发送→HAL IN；缓冲区必须跨异步阶段有效 |
| BSP_USB_Receive | 端点/指针/字节数，SysResult_t | Core接收→HAL OUT；预备接收不是已经收到 |
| BSP_USB_GetRxSize | EP0 OUT地址，uint32_t | LL查询→OUT_ep.xfer_count；0也可能是无效输入 |

部分BSP包装把未ready和非法端点共同返回INVALID_ARG，另一些返回INVALID_STATE/NOT_SUPPORTED。这是当前实际实现，不应在教学中凭理想接口语义改写其返回规则。需要精确行为时读对应函数条件。

### 11.2 Platform接口与内部辅助

| 接口/函数 | 参数、返回与作用 | 上下文和使用边界 |
|---|---|---|
| USB_Device_Init | 无参，SysResult_t，建立Core并注册基础Class | DEVICE Initcall，幂等 |
| USB_Device_RegisterClass | Class和描述符静态指针，SysResult_t | 初始化后、启动前；不是热切换接口 |
| USB_Device_Start/Stop | 无参，SysResult_t | 管理Core/BSP生命周期与错误合并 |
| USB_Device_IsConfigured | 无参，bool | 启动且无锁存错误且Core CONFIGURED；非实板验收结论 |
| USB_Device_GetLastResult | 无参，SysResult_t | 最近锁存的LL失败，不是所有错误的统计 |
| Usb_LlResult | SysResult_t→USBD状态 | LL适配使用；失败保存负数原因 |
| Usb_AutoStart | 无参，SysResult_t | APPLICATION等级调用USB_Device_Start |
| USB_Device_Alloc/Free | 长度→指针；指针→void | 固定1024字节单块；不使用malloc |
| Usb_DeviceDesc/Usb_LangDesc | speed、length输出→静态字节指针 | Core描述符请求链，多在USB IRQ |
| Usb_Manufacturer/Product/Serial | speed、length输出→字符串描述符 | 使用共享64字节静态缓存，序列号当前固定 |
| Usb_ConfigString/InterfaceString | 同上 | 字符串表提供接口，不等于配置中一定引用相应索引 |
| Usb_ConfigDesc/Usb_NoDesc | length输出→描述符或NULL | FS配置18字节；不提供HS/其他速度描述符 |
| Usb_ClassInit/DeInit | device、config→USBD结果 | 主机配置/复位等Core链；基础Class无额外端点 |
| Usb_ClassSetup | device、request→USBD结果 | 接口0标准查询，其他请求STALL |

`USBD_LL_Init/DeInit/Start/Stop/OpenEP/CloseEP/FlushEP/StallEP/ClearStallEP/IsStallEP/SetUSBAddress/Transmit/PrepareReceive/GetRxDataSize/Delay` 分别连接同名语义的BSP操作或HAL_Delay，参数中的设备对象必须对应本模块实例（Delay无设备参数）。这些是Core向下调用的接口。

`Usb_Setup/DataOut/DataIn/Reset/Suspend/Resume/Connect/Disconnect` 则把BSP事件向上转给 `USBD_LL_SetupStage/DataOutStage/DataInStage/SetSpeed+Reset/Suspend/Resume/DevConnected/DevDisconnected`，正常硬件事件路径运行于ISR。

### 11.3 当前描述符逐字段对照

| 设备描述符字段 | 当前值 | 解释 |
|---|---|---|
| bLength / bDescriptorType | 18 / 1 | 有效长度与设备描述符类型 |
| bcdUSB | 0x0200 | 声明USB 2.00规范版本，不表示480Mbit/s |
| bDeviceClass/SubClass/Protocol | 0/0/0 | 功能类别在接口中声明 |
| bMaxPacketSize0 | 64 | EP0最大包长 |
| idVendor/idProduct | 0x1209/0x0001 | 内部测试身份，不唯一 |
| bcdDevice | 0x0100 | 设备发布版本 |
| iManufacturer/iProduct/iSerialNumber | 1/2/3 | 字符串索引，不是字符串长度 |
| bNumConfigurations | 1 | 一个配置 |

| 配置及接口字段 | 当前值 | 解释 |
|---|---|---|
| wTotalLength | 18 | 9字节配置＋9字节接口 |
| bNumInterfaces / bConfigurationValue | 1/1 | 一个接口，主机用值1选择配置 |
| iConfiguration | 0 | 当前不引用配置字符串，虽提供对应查询函数 |
| bmAttributes | 0x80 | 总线供电声明，不声明远程唤醒 |
| bMaxPower | 250 | 单位2mA，声明最大500mA，不是实际电流测量 |
| bInterfaceNumber / bAlternateSetting | 0/0 | 接口0，备用设置0 |
| bNumEndpoints | 0 | 不包括EP0，无非零端点 |
| bInterfaceClass | 0xFF | 厂商自定义接口 |
| iInterface | 0 | 当前不引用接口名称字符串 |

配置前后电流与多路供电必须在实板核对，不能以描述符写了500mA就认定电源或协议合规。测试VID/PID使用边界见[pid.codes测试编号说明](https://pid.codes/1209/0001/)。

### 11.4 函数定位索引

以下索引由当前正式源码自动提取，仅索引函数定义，不把头文件声明误当实现。先看调用链，再点开对应函数阅读。

#### CM7/BSP/bsp_usb.c

| 函数 | 定义位置 |
|---|---|
| `BSP_USB_Init` | [第27行](D:/32/STM32H7/h7_mac/CM7/BSP/bsp_usb.c:27) |
| `BSP_USB_SetCallbacks` | [第79行](D:/32/STM32H7/h7_mac/CM7/BSP/bsp_usb.c:79) |
| `BSP_USB_Start` | [第92行](D:/32/STM32H7/h7_mac/CM7/BSP/bsp_usb.c:92) |
| `BSP_USB_Stop` | [第110行](D:/32/STM32H7/h7_mac/CM7/BSP/bsp_usb.c:110) |
| `BSP_USB_DeInit` | [第124行](D:/32/STM32H7/h7_mac/CM7/BSP/bsp_usb.c:124) |
| `BSP_USB_OpenEp` | [第142行](D:/32/STM32H7/h7_mac/CM7/BSP/bsp_usb.c:142) |
| `BSP_USB_CloseEp` | [第151行](D:/32/STM32H7/h7_mac/CM7/BSP/bsp_usb.c:151) |
| `BSP_USB_FlushEp` | [第158行](D:/32/STM32H7/h7_mac/CM7/BSP/bsp_usb.c:158) |
| `BSP_USB_StallEp` | [第165行](D:/32/STM32H7/h7_mac/CM7/BSP/bsp_usb.c:165) |
| `BSP_USB_ClearStallEp` | [第172行](D:/32/STM32H7/h7_mac/CM7/BSP/bsp_usb.c:172) |
| `BSP_USB_IsStallEp` | [第179行](D:/32/STM32H7/h7_mac/CM7/BSP/bsp_usb.c:179) |
| `BSP_USB_SetAddress` | [第186行](D:/32/STM32H7/h7_mac/CM7/BSP/bsp_usb.c:186) |
| `BSP_USB_Transmit` | [第193行](D:/32/STM32H7/h7_mac/CM7/BSP/bsp_usb.c:193) |
| `BSP_USB_Receive` | [第200行](D:/32/STM32H7/h7_mac/CM7/BSP/bsp_usb.c:200) |
| `BSP_USB_GetRxSize` | [第207行](D:/32/STM32H7/h7_mac/CM7/BSP/bsp_usb.c:207) |
| `HAL_PCD_MspInit` | [第215行](D:/32/STM32H7/h7_mac/CM7/BSP/bsp_usb.c:215) |
| `HAL_PCD_MspDeInit` | [第236行](D:/32/STM32H7/h7_mac/CM7/BSP/bsp_usb.c:236) |
| `OTG_FS_IRQHandler` | [第245行](D:/32/STM32H7/h7_mac/CM7/BSP/bsp_usb.c:245) |
| `HAL_PCD_SetupStageCallback` | [第251行](D:/32/STM32H7/h7_mac/CM7/BSP/bsp_usb.c:251) |
| `HAL_PCD_DataInStageCallback` | [第257行](D:/32/STM32H7/h7_mac/CM7/BSP/bsp_usb.c:257) |
| `HAL_PCD_DataOutStageCallback` | [第263行](D:/32/STM32H7/h7_mac/CM7/BSP/bsp_usb.c:263) |
| `HAL_PCD_ResetCallback` | [第269行](D:/32/STM32H7/h7_mac/CM7/BSP/bsp_usb.c:269) |
| `HAL_PCD_SuspendCallback` | [第275行](D:/32/STM32H7/h7_mac/CM7/BSP/bsp_usb.c:275) |
| `HAL_PCD_ResumeCallback` | [第281行](D:/32/STM32H7/h7_mac/CM7/BSP/bsp_usb.c:281) |
| `HAL_PCD_ConnectCallback` | [第287行](D:/32/STM32H7/h7_mac/CM7/BSP/bsp_usb.c:287) |
| `HAL_PCD_DisconnectCallback` | [第293行](D:/32/STM32H7/h7_mac/CM7/BSP/bsp_usb.c:293) |
| `Usb_ValidEp` | [第304行](D:/32/STM32H7/h7_mac/CM7/BSP/bsp_usb.c:304) |
| `Usb_HalResult` | [第309行](D:/32/STM32H7/h7_mac/CM7/BSP/bsp_usb.c:309) |
| `Usb_ClockInit` | [第320行](D:/32/STM32H7/h7_mac/CM7/BSP/bsp_usb.c:320) |

#### CM7/Platform/usb_device.c

| 函数 | 定义位置 |
|---|---|
| `USB_Device_Init` | [第159行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:159) |
| `USB_Device_RegisterClass` | [第180行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:180) |
| `USB_Device_Start` | [第205行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:205) |
| `USB_Device_Stop` | [第224行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:224) |
| `USB_Device_IsConfigured` | [第238行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:238) |
| `USB_Device_GetLastResult` | [第243行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:243) |
| `USBD_LL_Init` | [第249行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:249) |
| `USBD_LL_DeInit` | [第259行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:259) |
| `USBD_LL_Start` | [第266行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:266) |
| `USBD_LL_Stop` | [第273行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:273) |
| `USBD_LL_OpenEP` | [第280行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:280) |
| `USBD_LL_CloseEP` | [第287行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:287) |
| `USBD_LL_FlushEP` | [第294行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:294) |
| `USBD_LL_StallEP` | [第301行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:301) |
| `USBD_LL_ClearStallEP` | [第308行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:308) |
| `USBD_LL_IsStallEP` | [第315行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:315) |
| `USBD_LL_SetUSBAddress` | [第320行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:320) |
| `USBD_LL_Transmit` | [第327行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:327) |
| `USBD_LL_PrepareReceive` | [第334行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:334) |
| `USBD_LL_GetRxDataSize` | [第341行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:341) |
| `USBD_LL_Delay` | [第346行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:346) |
| `USB_Device_Alloc` | [第351行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:351) |
| `USB_Device_Free` | [第359行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:359) |
| `Usb_AutoStart` | [第371行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:371) |
| `Usb_LlResult` | [第376行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:376) |
| `Usb_Setup` | [第384行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:384) |
| `Usb_DataOut` | [第389行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:389) |
| `Usb_DataIn` | [第394行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:394) |
| `Usb_Reset` | [第399行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:399) |
| `Usb_Suspend` | [第405行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:405) |
| `Usb_Resume` | [第410行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:410) |
| `Usb_Connect` | [第415行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:415) |
| `Usb_Disconnect` | [第420行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:420) |
| `Usb_DeviceDesc` | [第425行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:425) |
| `Usb_LangDesc` | [第432行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:432) |
| `Usb_Manufacturer` | [第439行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:439) |
| `Usb_Product` | [第446行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:446) |
| `Usb_Serial` | [第453行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:453) |
| `Usb_ConfigString` | [第460行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:460) |
| `Usb_InterfaceString` | [第467行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:467) |
| `Usb_ConfigDesc` | [第474行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:474) |
| `Usb_NoDesc` | [第480行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:480) |
| `Usb_ClassInit` | [第486行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:486) |
| `Usb_ClassDeInit` | [第492行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:492) |
| `Usb_ClassSetup` | [第499行](D:/32/STM32H7/h7_mac/CM7/Platform/usb_device.c:499) |

#### Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c

| 函数 | 定义位置 |
|---|---|
| `HAL_PCD_Init` | [第123行](D:/32/STM32H7/h7_mac/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:123) |
| `HAL_PCD_Start` | [第996行](D:/32/STM32H7/h7_mac/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:996) |
| `HAL_PCD_Stop` | [第1021行](D:/32/STM32H7/h7_mac/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1021) |
| `HAL_PCD_IRQHandler` | [第1049行](D:/32/STM32H7/h7_mac/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1049) |
| `HAL_PCD_EP_Open` | [第1766行](D:/32/STM32H7/h7_mac/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1766) |
| `HAL_PCD_EP_Receive` | [第1843行](D:/32/STM32H7/h7_mac/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1843) |
| `HAL_PCD_EP_Transmit` | [第1884行](D:/32/STM32H7/h7_mac/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1884) |
| `PCD_WriteEmptyTxFifo` | [第2134行](D:/32/STM32H7/h7_mac/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:2134) |
| `PCD_EP_OutXfrComplete_int` | [第2194行](D:/32/STM32H7/h7_mac/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:2194) |
| `PCD_EP_OutSetupPacket_int` | [第2306行](D:/32/STM32H7/h7_mac/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:2306) |

#### Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd_ex.c

| 函数 | 定义位置 |
|---|---|
| `HAL_PCDEx_SetTxFiFo` | [第70行](D:/32/STM32H7/h7_mac/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd_ex.c:70) |
| `HAL_PCDEx_SetRxFiFo` | [第112行](D:/32/STM32H7/h7_mac/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd_ex.c:112) |

#### Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c

| 函数 | 定义位置 |
|---|---|
| `USB_CoreInit` | [第83行](D:/32/STM32H7/h7_mac/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:83) |
| `USB_DevInit` | [第300行](D:/32/STM32H7/h7_mac/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:300) |
| `USB_EPStartXfer` | [第743行](D:/32/STM32H7/h7_mac/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:743) |
| `USB_WritePacket` | [第979行](D:/32/STM32H7/h7_mac/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:979) |
| `USB_ReadPacket` | [第1010行](D:/32/STM32H7/h7_mac/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:1010) |
| `USB_SetDevAddress` | [第1153行](D:/32/STM32H7/h7_mac/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:1153) |
| `USB_DevConnect` | [第1168行](D:/32/STM32H7/h7_mac/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:1168) |
| `USB_DevDisconnect` | [第1185行](D:/32/STM32H7/h7_mac/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:1185) |

#### Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c

| 函数 | 定义位置 |
|---|---|
| `USBD_Init` | [第92行](D:/32/STM32H7/h7_mac/Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c:92) |
| `USBD_RegisterClass` | [第202行](D:/32/STM32H7/h7_mac/Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c:202) |
| `USBD_Start` | [第384行](D:/32/STM32H7/h7_mac/Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c:384) |
| `USBD_Stop` | [第400行](D:/32/STM32H7/h7_mac/Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c:400) |
| `USBD_SetClassConfig` | [第465行](D:/32/STM32H7/h7_mac/Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c:465) |
| `USBD_LL_SetupStage` | [第546行](D:/32/STM32H7/h7_mac/Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c:546) |
| `USBD_LL_DataOutStage` | [第586行](D:/32/STM32H7/h7_mac/Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c:586) |
| `USBD_LL_DataInStage` | [第681行](D:/32/STM32H7/h7_mac/Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c:681) |
| `USBD_LL_Reset` | [第771行](D:/32/STM32H7/h7_mac/Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c:771) |

#### Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c

| 函数 | 定义位置 |
|---|---|
| `USBD_StdDevReq` | [第104行](D:/32/STM32H7/h7_mac/Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c:104) |
| `USBD_GetDescriptor` | [第428行](D:/32/STM32H7/h7_mac/Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c:428) |
| `USBD_SetAddress` | [第682行](D:/32/STM32H7/h7_mac/Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c:682) |
| `USBD_SetConfig` | [第723行](D:/32/STM32H7/h7_mac/Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c:723) |
| `USBD_ParseSetupRequest` | [第950行](D:/32/STM32H7/h7_mac/Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c:950) |

#### Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ioreq.c

| 函数 | 定义位置 |
|---|---|
| `USBD_CtlSendData` | [第86行](D:/32/STM32H7/h7_mac/Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ioreq.c:86) |
| `USBD_CtlPrepareRx` | [第130行](D:/32/STM32H7/h7_mac/Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ioreq.c:130) |
| `USBD_CtlSendStatus` | [第171行](D:/32/STM32H7/h7_mac/Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ioreq.c:171) |
| `USBD_CtlReceiveStatus` | [第188行](D:/32/STM32H7/h7_mac/Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ioreq.c:188) |


## 十二、资料与验证边界

### 12.1 推荐对照资料

1. [USB-IF USB 2.0规范入口](https://www.usb.org/document-library/usb-20-specification)：重点阅读数据流模型、协议层、USB设备框架，理解端点、事务、标准请求和控制传输。
2. [ST RM0399参考手册](https://www.st.com/resource/en/reference_manual/rm0399-stm32h745755-and-stm32h747757-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)：查USB OTG章节、寄存器及RCC/CRS相关章节。在线工具本轮未能完整展开大PDF；本文具体寄存器动作以已读取的本地HAL/LL交叉核对，不声称逐页核验整本RM。
3. [ST STM32H747数据手册](https://www.st.com/resource/en/datasheet/DM00602212.pdf)：查控制器/PHY能力、引脚、电源和USB电气条件。
4. [ST AN4879](https://www.st.com/resource/en/application_note/an4879-introduction-to-usb-hardware-and-pcb-guidelines-using-stm32-mcus-stmicroelectronics.pdf)：查PHY、供电、VBUS检测及硬件连接注意点。
5. [Arm USB协议说明](https://www.keil.com/pack/doc/mw/USB/html/_u_s_b__protocol.html)：主机调度、传输类型和方向的简要说明。
6. 工程的 [README.md](D:/32/STM32H7/h7_mac/Tests/UsbDeviceStage1/README.md)：确认本项目已经验证了什么、尚缺什么。

这些资料用于核对规则，实际调用顺序仍以本工程这版源码为准。不要从网上另一个版本摘函数后假定当前项目完全相同。

### 12.2 当前已验证与未验证

上一轮记录：ARMCC5 USB隔离模拟52/52通过、428次断言；CM7/CM4正式Target均0 Error、0 Warning。这是已有验收记录，本次教学文档没有重新运行编译或硬件测试。

模拟器执行真实BSP、Platform和ST Core，但HAL PCD、RCC、GPIO、NVIC等使用Mock，能验证软件参数、回调链、请求处理和状态；不能证明PHY电气、USB时钟精度、真实中断时序和Windows枚举成功。

仍需实板验证：供电、PA11/PA12连接、USB线、CRS稳定、真实主机请求/状态阶段、插拔/挂起/复位，以及相机并行运行时的响应。设备树显示的身份、完整描述符、SET_ADDRESS/SET_CONFIGURATION成功及正常运行记录，才能形成真实枚举验收证据。

### 12.3 建议的实际阅读路线

第一遍只读自己的两份C文件，认清“向下接口”和“向上回调”，不急着记全部寄存器。

第二遍沿 `BSP_USB_Init → HAL_PCD_Init → USB_CoreInit/USB_DevInit` 看初始化；沿 `BSP_USB_Start → HAL_PCD_Start → USB_DevConnect` 看启动。检查Init成功、PCD READY和Core CONFIGURED为什么不同。

第三遍用本文18字节GET_DESCRIPTOR，把SETUP原始数据、Core解析字段、描述符指针、EP0阶段、FIFO搬运和完成回调串起来。发送指针可能已被HAL推进，不要总把中断回调里的指针当作原始缓冲区起点。

第四遍在实板上用有限断点和主机工具对应这些阶段。能够说清“哪个软件状态变化，哪项硬件事件证明什么，哪些事情仍未证明”，比只记住HAL函数名更有用。

本文仅新增教学说明，不修改BSP、Platform、Core、HAL、相机或应用逻辑，不继续实现UVC。
