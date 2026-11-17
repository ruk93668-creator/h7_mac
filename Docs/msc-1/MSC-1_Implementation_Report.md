# MSC-1 独立 MSC 枚举实施报告

日期：2026-11-17；分支 codex/usb-msc-bringup；起始本地及实时远端均为 `0d81ba137bd38dded3c8dcda56abae26d0cd3fd9`，工作区干净。

## 结果与范围

已实现独立枚举 Class：设备级class0；一个interface0/alt0，08/06/50；EP82 Bulk IN、EP02 Bulk OUT，FS MPS64；配置长度32（9+9+7+7），bNumInterfaces1、bNumEndpoints2。描述符持久保存，PIO尾部填充不计入传输长度。仅FS，HS/qualifier/other-speed不提供。

Class通过现有USB_Device_RegisterClass注册；SET_CONFIGURATION时以USB_Device_OpenEp打开两端点；第二端点打开失败尝试清理；DeInit分别关闭并保留失败端点标记以支持重试。静态状态且没有在途数据buffer，不使用唯一1024字节Class pool。标准GET_STATUS/GET_INTERFACE/SET_INTERFACE严格检查方向、完整wIndex、wValue和长度；仅Alt0。Core的CLEAR_FEATURE回调只作确认，不重复发送状态包。

未实现BOT、CBW/CSW、SCSI、LUN或存储访问。GET_MAX_LUN与Bulk-Only Reset明确STALL；Bulk OUT从未arm接收，Bulk IN从不提交，因此不能读写磁盘。Windows可能绑定USB Mass Storage驱动后继续探测并报告启动/介质错误，这不等于枚举描述符失败，也不能宣称MSC协议已工作。

## 启动与构建

新增 `usb_msc_class.c/.h`、`usb_msc_desc.c/.h`。现有修改仅usb_mode.c/.h、app_main.c、video_config.c、CM7 Keil源文件列表及本地生成物gitignore。ST Core、Platform/BSP USB实现、全部UVC/Bridge、SDMMC/FatFS/StorageTask、MPU/linker/FreeRTOS/M4均未改。MSC-0七份文档原样保留。

`USB_MODE_BOOT_MSC=1`为专用冷启动编译开关（默认0，与BOOT_UVC互斥）。video_config选择现有USB_TEST无视频输出路径，模式层将这次TEST选择映射到MSC并注入USB_MSC_Register回调；没有修改UVC Bridge，也没有增加Composite。外部显式选择MSC必须提供注册回调。默认正式工程继续原UVC/MJPEG配置；只增加两个MSC源文件到CM7组，CM4组及目标配置未改。

运行 `python Tests/MscEnumeration/build.py` 生成临时MSC工程，完整重编MSC与默认UVC两个CM7配置，结束删除自身临时工程并核对正式工程字节未改变。最终默认Keil产物是原UVC模式；需要板测请使用独立 `output/MSC1/msc.axf`，或在CM7编译定义中设置USB_MODE_BOOT_MSC=1后自行重编。可烧录HEX为 `output/MSC1/msc.hex`（fromelf转换）。测试身份1209:0001仅内部使用，无序列号；不能用于产品分发或当作最终生产身份。

## 软件验证证据

- ARMCC5编译真实MSC Class/descriptor，Unicorn执行34项断言全部通过：长度/字段/地址、FS包长、拒HS、端点打开失败回收、关闭失败保留/重试、无效CDB类请求拒绝、标准接口请求严格检查。脚本run_tests.py，results.json。
- 集成测试运行真实Mode/Core/Platform/BSP与MSC，HAL硬件桩：30项断言通过，覆盖专用启动开关、主机GET_CONFIG、SET_CONFIGURATION、Bulk端点状态/FIFO、GET_MAX_LUN STALL、总线Reset重配和Stop资源释放。脚本run_integration.py，integration_results.json。HAL桩以当前RX320/TX布局及端点maxpacket副作用建模，不能代表实际硬件时序。
- 最终实现两种CM7完整重编均为0 Error(s), 0 Warning(s)。build_results.json含产物SHA256，source_hashes.json记录审核源码指纹；完整日志/AXF/map及依赖在被忽略的output/MSC1与output/msc1-deps中。

### UVC回归的实际限制

原有十套脚本分别在当前源码和起始提交副本运行，未覆盖原历史报告。`run_regression.py`复制到本目录output隔离工作区；`--baseline`以git show恢复四个被修改的既有生产文件。`compare_regression.py`逐用例和summary比较，九份可执行结果完全相同；不存在新增失败，但不能报告UVC回归全通过。

|套件|基线与当前通过/总数|
|---|---|
|Streaming|1/36|
|Payload|122/124（O0/O2各一项旧513包长拒绝预期失败）|
|EP1|0/45|
|Probe|0/89|
|Cancellation|0/39|
|Core|201/204|
|EP0 IRQ Race|4/4|
|Lifecycle|34/40|
|Cleanup contract|0/14|
|Descriptors|主unittest缺camera_mjpeg_fifo.h include路径而未执行；附带string子测试两边均8/8|

主要旧失败依据：UVC生产代码现在要求先USB_UVC_Configure，旧测试直接Register得到INVALID_STATE；部分历史HAL桩仍假定旧FIFO资源；Payload的513包长拒绝预期与当前通用实现不符。此阶段不修改这些UVC模块或历史测试。基线等价只能证明这些已执行断言没有退化，不能代替有效的全绿UVC验收，需后续授权更新测试适配。证据为baseline_regression_results.json、current_regression_results.json、regression_comparison.json及output内逐套日志。

## 实板验收：NOT_TESTED

本次未连接/操作板卡，未烧录；没有Windows设备管理器或USBPcap结果。

人工验收步骤：烧录MSC专用AXF/HEX；Windows重连，在USBView核对Device/Configuration与interface0的08/06/50、EP82/02 Bulk64；USBPcap记录复位、取设备/配置、设置地址与配置过程；确认主机看见Mass Storage Interface。保存抓包和VID/PID/实际描述符及驱动状态，不要求磁盘可读写；记录后续Class请求STALL是本阶段边界。再烧录默认UVC构建执行原视频档位验证。主机缓存同一内部测试VID/PID时应记录旧设备实例影响，不能误报协议支持。

## Git范围与停止点

只提交上列MSC-1实现、测试脚本/结果和本报告；不提交依赖、构建产物或原回归报告覆盖。不合并main，不修改其他工作目录。硬件与有效全绿UVC回归尚未完成，提交明确标记为WIP/待硬件验收，推送后核对本地/远端SHA。回滚可撤销本阶段提交，默认UVC代码未改变；不要将MSC-1用于真实磁盘数据。

本阶段停止，等待人工审核，不进入MSC-2。

## 2026-11-17 代码规范整理

对照主线usb_test_device.c与usb_uvc_payload.c整理MSC新增模块：模块职责与静态状态采用中文注释；内部函数改为Msc_前缀，先声明、再公开接口、最后内部实现；大括号独立行，分开变量声明与多条语句；描述符按字段注释，使用USB描述符常量、VID/PID常量及无符号字面量；头文件补齐接口注释；启动接入整理排版并统一CRLF，保留既有文件编码。

描述符字节、接口/端点地址、控制请求判断及端点生命周期行为保持不变。复用现有34项Class测试与30项真实栈集成断言，全部通过；重编MSC和默认UVC两个CM7配置，结果记录于更新后的build_results.json。未修改UVC/Core/SDMMC/FatFS/MPU或Keil源文件清单，未开展MSC-2。此前实板及历史回归限制继续有效。

## 2026-11-17 实板复位后描述符验证

此记录更新前文实板NOT_TESTED状态中的描述符/当前连接部分；完整枚举时序抓包仍未通过工具验证。

用户复位板卡后，通过Windows USB Hub的只读IOCTL读取目标端口，VID/PID为1209:0001。实际Device Descriptor为18字节；Configuration为32字节，接口0/Alt0、端点数2、类别08/06/50；EP82 IN和EP02 OUT均Bulk、wMaxPacketSize64。连接信息报告Full-Speed、CurrentConfigurationValue=1、NumberOfOpenPipes=2、DeviceConnected。最新PnP快照为“USB大容量存储设备”、Status OK、ProblemCode 0。此前曾观察Code 10，本次复位后快照不等于BOT/SCSI已经支持。

实际配置字节：`09 02 20 00 01 01 00 80 fa 09 04 00 00 02 08 06 50 00 07 05 82 02 40 00 00 07 05 02 02 40 00 00`。机器可读证据见Tests/MscEnumeration/hardware_results.json，未纳入主机其他USB设备身份或流量。

Wireshark/tshark尝试通过USBPcap1捕获复位枚举，但报“magic=0x00000000，非有效pcap/pcapng”，0 packets captured；不能视为有效抓包，也不能声称观察到线上SET_CONFIGURATION成功包。Hub当前配置值1只证明当前配置状态。结论：MSC-1实板描述符与端点配置检查通过，USBPcap完整枚举时序证据仍待补充；无磁盘读写验收，不进入MSC-2。
