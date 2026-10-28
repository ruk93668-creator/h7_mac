# USB Mode Manager 基础框架验收

日期：2026-10-28。范围：TEST启动管理、初始化注册和主循环占位。不实现UVC/MSC业务或运行时切换。

## 正式工程修改

- `CM7/Platform/usb_device.c`：删除旧`Usb_AutoStart`声明、定义及APPLICATION注册；保留DEVICE等级的`USB_Device_Init`和全部原有公开接口。
- `CM7/Platform/usb_mode.h`：新增TEST/UVC/MSC模式、管理状态枚举及四个公开接口。
- `CM7/Platform/usb_mode.c`：新增私有`s_usb_mode`上下文、SERVICE初始化、APPLICATION启动和Process占位。
- `CM7/Core/Src/main.c`：在USER CODE Includes中包含新头文件，在USER CODE 0的`Main_Run()`中调用`USB_Mode_Process()`；保持Framework_Run、App_Run调度。
- `MDK-ARM/h7_mac.uvprojx`：两个Target的CM7/Platform工程树加入新C/H；CM7参与构建，CM4继续沿用该组IncludeInBuild=0。已有Platform Include Path覆盖新头文件，无需重复增加。

没有修改正式BSP、ST Middleware、HAL、描述符、VID/PID、FIFO、端点配置或CM4业务代码。保留任务开始前用户已有的未提交修改。

## 初始化链

```text
DEVICE       USB_Device_Init -> 注册现有TEST Class，不启动
SERVICE      USB_Mode_Init -> TEST / IDLE
APPLICATION  UsbMode_AppStart -> USB_Mode_Start -> USB_Device_Start
主循环       Framework_Run -> USB_Mode_Process -> App_Run
```

正常初始化只有一个自动启动入口。重复USB_Mode_Init保持当前状态；重复USB_Mode_Start返回SYS_ERR_BUSY，不再次调用Core启动。RUNNING表示软件启动成功，不表示主机已经完成枚举。

## 接口行为

| 接口 | 当前行为 |
|---|---|
| USB_Mode_Init | 初始化TEST、IDLE和结果；幂等，不重复初始化Core |
| USB_Mode_Start | 只允许TEST且IDLE；调用既有Core启动；失败保留原错误并进入ERROR，不自动重试 |
| USB_Mode_RequestReconnect | 未初始化返回INVALID_STATE；初始化后返回NOT_SUPPORTED；不记录请求、不停止或启动硬件 |
| USB_Mode_Process | 未初始化直接返回；已初始化仅有TODO，不改变状态，不执行切换 |

留给用户实现：

1. 重连请求、目标模式记录及请求接收条件。
2. STOPPING、WAIT_RECONNECT、REGISTERING等阶段的状态转换和非阻塞断开等待。
3. 停止、Class注册、重新启动失败后的恢复策略。
4. 后续UVC/MSC描述符、Class及业务对接。本次只预留枚举，不提供选择这些模式的公开接口。

## 软件测试

执行：

```powershell
python Tests/UsbModeManager/run_tests.py
```

编译器：ARMCC V5.06 update 7 build 960；正式Mode Manager、USB BSP/Platform、Framework Initcall和ST Core编译为Cortex-M7代码，用Unicorn执行，HAL硬件部分使用既有隔离桩。

结果：**64/64 PASS，560次断言**。包含12项Mode Manager用例与52项适配当前时钟前置条件的USB回归。每项实际结果见results.json，实际编译链接命令及版本见commands.json。

修改前运行旧`Tests/UsbDeviceStage1/run_tests.py`得到4 PASS / 48 FAIL：原有时钟桩不设置RCC的USB源与HSI48RDY，而用户当前正式BSP已改为直接检查这些寄存器。这是任务开始前即可复现的测试适配问题。旧脚本及用例源码没有修改。

本轮测试入口先准备模拟RCC的HSI48选择和就绪状态；旧第5项由“名义频率错误”适配成“HSI48未就绪”，清除就绪位以触发正式BSP的实际检查，不修改正式BSP或掩盖失败。其他旧场景复用原测试实现。

新增覆盖：未初始化拒绝启动、Init幂等、重复Start、运行中重复Init、Reconnect占位无副作用、Process调用1000次不自动操作、启动失败保留ERROR、Core未初始化、UVC/MSC明确未支持、实际初始化段等级、Manager启动后的产品名称及VID/PID/配置请求、预留状态下Process无动作。

产品名称保持`h7_mac USB FS Test`。该字符串实际为18个字符，USB字符串描述符为38字节；本次逐字符核对UTF-16LE内容。

## 正式CM7编译

```powershell
D:\32\UV4\UV4.exe -b D:\32\STM32H7\h7_mac\MDK-ARM\h7_mac.uvprojx -t h7_mac_CM7 -o D:\32\STM32H7\h7_mac\Tests\UsbModeManager\cm7_keil.log
```

实际构建为Keil CM7 Target Build（增量构建），**0 Error / 0 Warning**。构建日志见cm7_keil.log；Map中确认新对象和SERVICE/APPLICATION注册项均被链接，旧Usb_AutoStart不存在。

未重新编译CM4；已核对XML中CM4排除CM7/Platform组，未改其业务源码。没有连接USB主机、烧录或进行实板切换/枚举验收。模拟的描述符请求成功不能证明Windows枚举已实测通过。
