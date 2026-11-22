# BLE-B0 硬件与架构审计报告

- 日期：2026-11-22（Asia/Shanghai）；分支：`codex/ble-rtl8723bs`。
- 被审计生产基线：`f0ce45fd9ab7250b1e22260e087d606ee5ed9ecb`。
- 本报告是 Codex 的审计提交，最终验收由用户独立审核；**没有 B1 开发授权**。
- 本轮仅增加报告、证据清单与只读采集脚本；没有驱动、协议栈、工程配置、烧录或实板测试变更。
- PASS 仅用于明确限定的证据项；NEEDS_FIX 表示现方案需要修订；BLOCKED 表示当前后续准入条件不成立；NOT_VERIFIED 表示无实测或证据不足。

## 1. 审计结论与准入状态

**建议整体结论：BLOCKED（后续开发准入），等待独立审核。B0 桌面调查已提交，B0 实板验收未完成；不得据此进入 B1。**

| 编号 | 范围 | 建议状态 | 已有证据与缺口 |
|---|---|---|---|
| B0.1 | 硬件 | NOT_VERIFIED | 原理图 UART/CTS/上拉连接已核对；实板连通、电压、启动和 RF 未测 |
| B0.2 | 资源冲突 | NOT_VERIFIED | 当前源码未见 USART2 占用；DMA/IRQ 延迟和其他支线联合配置未验收 |
| B0.3 | H5 | NEEDS_FIX | 需明确偶校验、完整可靠传输及 CRC 策略；没有本板建链证据 |
| B0.4 | 固件 | BLOCKED | ROM/版本未知，未选定可分发 Patch/config；无可控硬复位或可靠恢复证据 |
| B0.5 | NimBLE | NEEDS_FIX | 官方 FreeRTOS NPL、任务创建及 GATT 堆分配与当前约束冲突；静态移植未实现、未编译 |
| B0.6 | 架构 | NEEDS_FIX | 当前基线只有 Application，ApplicationV3 在另一支线；需明确后续集成基线 |
| B0.7 | 隔离 | PASS（本轮变更范围） | 只新增 Docs/BLE 与 Tests/BleB0；联合运行兼容性仍 NOT_VERIFIED |

三个重点问题的直接答案：

1. **CTS 接地是否可靠可用？NOT_VERIFIED。** 它可能让模块持续认为主机允许发送，无法为 MCU 接收提供背压。H5 ACK/重传不等于硬件流控。必须验证特定 ROM、config、串口模式和最坏接收延迟。
2. **是否有可靠复位/下载/失败恢复？BLOCKED。** 原理图没有 MCU 控制 BT_DIS# 的路径。UART 外设复位或 HCI Reset 无法等同于模块掉电/ROM 复位；下载中途故障后能否重新开始未知。
3. **NimBLE 是否满足禁动态分配？官方适配不能直接满足。** 除 NPL 外，GATT 也有 libc 分配点。理论上可做固定容量适配，但尚未证明完整调用链、容量和 ARMCC5 兼容性，不能宣称已兼容。

## 2. 证据及复核方式

- 用户提供原理图 ZIP：`SVG_Schematic1_1_2026-11-16.zip`。页 2（主控）与页 7（调试与扩展）已渲染并人工核线；归档及各页 SHA-256 见 [schematic_manifest.json](../../Tests/BleB0/evidence/schematic_manifest.json)。不是 PCB 网表或实板连通报告。
- 网络截图：[模块接线](../../Tests/BleB0/evidence/module_wiring.png)、[MCU UART 引脚](../../Tests/BleB0/evidence/mcu_uart.png)。截图来源是上述 SVG 的 CairoSVG 渲染及裁剪。
- 当前源码带行号清单：[local_inventory.json](../../Tests/BleB0/evidence/local_inventory.json)。可运行 `python Tests/BleB0/collect_local_evidence.py` 重新生成到标准输出；它只读源码，不能证明硬件可用。
- 上游版本、URL、字节数及 SHA-256：[upstream_manifest.json](../../Tests/BleB0/evidence/upstream_manifest.json)。Linux 固定 v6.12，NimBLE 固定 nimble_1_8_0_tag，作为参考基线，不声称是最新版本。
- 2026-11-22 经 `git ls-remote --heads` 查询：远端 main 与 BLE 均为 `f0ce45f…`，远端 `codex/application-v3-flipper` 为 `6d07e6c99e545a4d207a312442835e39ee02c19a`。未合并任何其他支线，未触碰旁边工程的未提交修改。
- 未连接示波器、逻辑分析仪、串口适配器或调试探针；没有原始 HCI 抓包、ROM 响应、运行时栈水位或当前构建 map。因此这些证据项均不能标为 PASS。

## 3. B0.1 硬件

| 端点 | 原理图依据 | 桌面判断 / 实测缺口 |
|---|---|---|
| U12.43 UART_IN | WIFI/BLE_TX → U11 PD5（A11） | MCU TX；USART2 AF7 候选有效 |
| U12.42 UART_OUT | WIFI/BLE_RX → U11 PA3（U2） | MCU RX；USART2 AF7 候选有效 |
| U12.44 UART_CTS | 直接接 GND | 无 MCU 背压控制；有效电平/模块实际行为仍需模块资料和实测 |
| U12.34 BT_DIS# | R16 10 kΩ 上拉 VCC | 未见 MCU 控制；不能承诺独立复位 |
| U12.6 / .7 | BT_WAKE / BT_HOST_WAKE 分别经 R21/R28 10 kΩ 上拉 VCC | 未见 MCU WAKE 接线；不规划依赖 GPIO 的休眠唤醒 |
| U12.9 / .22 | VBAT、VDD_IO 均接 VCC；C71 10 μF 去耦 | 与板级电源共享，不能推断可单独断电 |
| U12.2 | WIFI/BT_ANT → J2 KH3216-G8 | 图纸有天线路径；装配、阻抗、匹配及 RF 性能未验证 |
| U12.24 | SUBSCLK_IN 图中未外接 | 模块内部时钟来源及外部低频时钟要求需实际模块资料确认 |

MCU 型号来自 `h7_mac.ioc:16`：STM32H747XIH6。ST [DS12930 Rev 3](https://www.st.com/resource/en/datasheet/stm32h747xi.pdf) 页 67/83（PDF 人读页码）列出 PA3 USART2_RX、PD5 USART2_TX；AF 表和本地 `stm32h7xx_hal_gpio_ex.h:197` 支持 AF7。AF 宏本身并非特定引脚已初始化的证明。

验收前需要：断电测 TX/RX/CTS 连通与无短路；上电测 VBAT/VDD_IO、BT_DIS#、WAKE 实际电平及启动时序；确认模块完整料号/厂商 pinout、CTS 极性、内部时钟、天线装配。不得仅依据 RTL8723BS 芯片名称推定封装模块所有 NC/电源脚语义。

## 4. B0.2 资源和双核审查

| 资源 | 当前基线证据 | 后续约束 |
|---|---|---|
| USART2 / IRQ38 | CM7/CM4/BSP/Platform 未找到 USART2 驱动或 IRQ 实现；CMSIS USART2_IRQn=38 | 仅建议 CM7 所有；需查寄存器、向量表和时钟门控实际状态 |
| PA3 / PD5 | 图纸专用于 UART；现 SDRAM 初始化未包含这两个引脚 | 不整端口重配或复位 GPIOA/GPIOD；不得误用 PD6（图纸为 SDIO_CLK） |
| 时钟 | main.c 设置 APB1 分频；ioc USART234578=120 MHz | ioc 不是运行时证据；B1 若获批，读取实际 kernel mux/分频并计算 BRR，不改 PLL1/PLL2 或共享 mux 破坏其他串口 |
| DMA | DCMI 占 DMA1_Stream0，优先级 5；SDMMC1 使用独立 IDMA | B1 首选评估 IRQ/FIFO，未分配任何新 DMA；若选 DMA 必须重新审查 stream/DMAMUX/总线/缓存 |
| IRQ | DCMI/DMA=5，USB FS=6，SDMMC1=7，TIM6=4；SysTick 为 RTOS tick | 调用 FreeRTOS FromISR 的 IRQ 数值须 >=5，不能使用普通阻塞 API；新 IRQ 优先级不在 B0 冻结 |
| CM4 | I2C2/OLED，I2C EV/ER IRQ=0（另一核） | 不修改 CM4，不跨核共享 UART，不新增 HSEM/IPC 所有权 |
| 时间/锁 | RTOS 1 ms，TIM6 HAL 时基；Platform_Sync 使用 PRIMASK/BASEPRI 边界 | 不挪用 TIM6/SysTick；测最长关中断时间，日志不得在 RX ISR 阻塞 |

115200、8 数据位+偶校验+1 停止位约 10473 字节/秒，2 KiB RX 环形缓冲理论上仅覆盖约 196 ms 的未消费时间；这是理想预算，不是安全保证。1.5 Mbps 时仅约 15 ms。UART FIFO 和 ISR 延迟比环形缓冲更先决定 ORE，必须同时量测，不能拿 2 KiB 容量替代关中断上限。

CM7 自定义 scatter 把普通 RW/ZI 放在 DTCM `0x20000000..0x2001FFFF`。若后续选择 DMA1/2，不能直接让 DMA 访问默认静态 DTCM 数组；须选择可达 SRAM，单独审查 MPU/cache、对齐和链接区域。现有 AXI SRAM 前 32 KiB 已给 `.sdmmc_dma`，不能直接借用。

## 5. B0.3 H5 协议方案（待审核，不是实现）

参考 [Linux v6.12 hci_h5.c](https://github.com/torvalds/linux/blob/v6.12/drivers/bluetooth/hci_h5.c) 的 link-control、序号和超时处理，以及 [Realtek 历史实现](https://github.com/lwfinger/rtl8723bs_bt/blob/09eb91f52a639ec5e4c5c4c98dc2afede046cf20/hciattach_rtk.c) 的 CRC 路径。Linux 代码仅供研究，不能直接把依赖 skb、workqueue 和 GPL 的实现拷入本工程。

- **串口启动参数：115200、偶校验。** Linux `h5_btrtl_open` 先关闭主机硬件流控并设置 even parity；这不能证明目标模块支持任意配置。STM32 HAL 需注意 WordLength 包含校验位的语义，8 数据位+校验不能误设为仅 7 数据位。
- 三线是 H5 协议名称，不表示 RTS/CTS 已接；本板只有 TX/RX 数据线和公共地。H5 窗口限制包数，无法停止已经到达 UART 的字节。
- 状态建议：UNINITIALIZED → SYNC → CONFIG → ACTIVE；只有完成双方参数协商才允许普通 HCI。处理 ACTIVE 中再次 SYNC，丢弃旧 epoch 待发/未 ACK 包，通知上层失联。
- 必须有 SLIP 边界/转义、4 字节头校验、12 位长度边界、可靠包 seq/累计 ACK（模 8）、ACK-only、合法窗口、重复包抑制、非法 ACK 和超时重传。初期只提议窗口 1；窗口、重试次数和时限需用目标实测冻结。
- 建链重试 100 ms、ACK 超时 250 ms 可作为 Linux 参考起点；建议候选最多 10 次建链/3 次数据重传，失败进入显式 FAILED，禁止无限等待。上述限值尚未验收。
- **CRC 不能照抄 Linux 的 `h5_rx_crc` 即认为完成**：该路径消费尾部后完成收包，没有实际比较 CRC。拟支持正确的协商与 CRC 校验；未支持 CRC 时不得宣告支持，收到不支持组合要失败/重协商，不能静默放行。算法、反转及字节序用历史驱动/规范和独立向量交叉验证。
- RX 坏头、非法转义、超长、截断或 ORE：计数、丢当前帧、回到边界搜索；不得将部分数据交 Host。序号异常重新 ACK 已确认位置，失步超限才重建链，不把重复可靠包再次交 Host。
- 不开启依赖 WAKE GPIO 的节能；但必须识别链路内 sleep/wakeup 消息或给出明确不支持策略，避免死等。

协议正确性仍 NOT_VERIFIED：没有本工程实现、向量测试或实板 H5 握手记录。

## 6. B0.4 固件加载与复位恢复

[Linux btrtl v6.12](https://github.com/torvalds/linux/blob/v6.12/drivers/bluetooth/btrtl.c) 的 8723BS UART 匹配项区分 USB 8723B，固件候选名为 `rtl8723bs_fw.bin`、`rtl8723bs_config.bin`。须通过 HCI 版本与 ROM 响应匹配，不能由丝印直接选 Patch。

建议审查流程：冷上电/已知复位 → H5 → Read Local Version（0x1001）与 ROM vendor read（参考 0xfc6d）→ 校验 epatch/project/ROM、config 边界 → 波特率 vendor 设置（参考 0xfc17）与两端切换确认 → 分块下载（参考 0xfc20）→ 响应/终片校验 → HCI Reset/版本与能力复查。每一步都验证 status、长度、opcode、超时和状态，不把“发完 UART”算作成功。

**具体反例：** 参考仓库固定提交 `09eb91f…` 的 55 字节 `rtlbt_config` 解析见 [candidate_config.json](../../Tests/BleB0/evidence/candidate_config.json)：UART 条目 offset=0x000c，速率码 `0x04928002`（Linux 映射 1.5 Mbps），data[12]=0x5f，bit2=1（启用流控）。它不是已批准的本板配置，也没有被集成。保持 115200 必须有经过验证的配置方案，不能忽略 blob 中的速率/流控设置。

CTS 接地若确为有效低，只能让模块发送端持续放行，不能证明模块接收端无需流控；无 RTS 输出与 MCU 连接时主机也没有控制器接收背压信号。必须覆盖双向突发、下载跨块、ACK 延迟及丢字节；优先小窗口、受控速率，不能承诺 H5 能补救所有丢字节。

复位缺口：Linux 路径控制 enable GPIO，并有复位与启动等待；本板 R16 仅上拉。MCU warm reset 后模块可能仍保留旧波特率/已加载 Patch/旧 H5 状态。HCI Reset 不是可靠恢复 ROM 下载状态的替代品。需分别证明冷启动、仅 MCU 复位、下载半途失败、重新建链的恢复；若只能人工整板断电，必须记为人工恢复并保留自动恢复阻塞项。加飞线/开关电源属于另行批准的硬件修改，本轮没有实施。

当前无目标固件二进制选择、ROM ID、完整模块资料或再分发授权链。历史仓库可下载不等于许可已通过；后续需记录供应商/上游、版本、SHA-256、板级 config、许可证原文、修改及分发条件，不先提交未知许可的 Patch。

## 7. B0.5 Host 与无堆内存

参考 NimBLE `nimble_1_8_0_tag`，不是 ESP32 专有移植层：

| 源码 | 可直接核查的调用/行为 | 当前冲突 |
|---|---|---|
| porting/npl/freertos/include/nimble/nimble_npl_os.h:91 | xQueueCreate | 动态分配禁用 |
| porting/npl/freertos/src/npl_os_freertos.c:151/205/277 | 创建递归 mutex、counting semaphore、software timer | 这些功能和动态创建目前均禁用 |
| porting/npl/freertos/src/nimble_port_freertos.c:49 | xTaskCreate Host | 应使用已审核的静态任务所有权 |
| nimble/host/src/ble_gatts.c:1200/1211/2051 | malloc / realloc 分配 GATT 数据 | 只改 NPL 仍不够；初始化期也不允许隐式堆 |
| porting/nimble/src/os_msys_init.c | 编译期数组的 mempool / mbuf pool | 提供固定池基础，但不覆盖所有分配路径 |
| nimble/transport/uart/src/hci_uart.c:31/55/80 | H4 transport 状态机 | 不能拿来直接替代 H5 |

本地 `FreeRTOSConfig.h:25..32` 为 static=1、dynamic=0、timers/mutex/recursive/counting=0。不得为迁就上游而直接改全局配置。

拟评估方案：保持 Host 上游核心，单独项目 NPL（固定事件队列、任务通知/有界同步、deadline callout，由唯一 Host 上下文执行回调），固定容量 GATT 注册存储与 mbuf/连接/传输池；所有分配点要逐一说明容量、生命周期、耗尽错误和回收。递归 mutex 的语义必须真实实现或证明可移除，不能用空锁替代；callout stop/reset/并发与 tick wrap 要测试。后续需全调用链分配审计、链接符号/重定位检查，并以运行期分配陷阱补证，不能靠一次 rg 搜索宣称无堆。

H5 在 Platform 与 Host transport 之间独立适配：只有控制器初始化成功才交 Host；明确 event/ACL buffer 所有权、背压、pool exhaustion、H5 ACK 后回收与重传保留，避免双重释放。Host callbacks 不直接驱动 Camera/USB/DMA。无本地 Controller/LL 需要，不引入 NimBLE controller task。

| 选型 | 参考实现/许可 | 代价判断 |
|---|---|---|
| Apache NimBLE | Apache-2.0；官方 FreeRTOS NPL 和固定 mbuf pool | 优先候选，但自定义 H5/NPL、GATT 无堆处理、syscfg 与 ARMCC5 构建均未验证 |
| BTstack | 官方 [H5 transport](https://github.com/bluekitchen/btstack/blob/9edb5d82ea6ec4466a393415d65030929330acdf/src/hci_transport_h5.c)、固定池配置能力；[LICENSE](https://github.com/bluekitchen/btstack/blob/9edb5d82ea6ec4466a393415d65030929330acdf/LICENSE) 含非商业限制 | 可能减少 H5 工作量，但商业相机用途须解决授权，不能默认可替换；本轮未做 footprint/构建测量 |

选择仍为“优先评估 NimBLE”，没有移植验收或最终冻结。

### 内存预算（设计上限草案，非实测占用）

现有 CM7 scatter：Flash 1 MiB；DTCM 普通 RW/ZI 128 KiB；AXI `.sdmmc_dma` 32 KiB；共享区 4 KiB。现有五任务+idle 静态栈共 `(1536+2048+768+512+2048+256)*4=28672 B`，不含 TCB、主栈及其他 ZI。

| 新增候选区域 | 字节预算 | 说明 |
|---|---:|---|
| B1 UART RX/TX rings | 2048+1024 | CPU IRQ 路径；DMA 方案另审 |
| B2 decoded frame | 1024 | 初期 ACL 分片上限须与 Host/controller 协商，不能静默截断 |
| B2 escaped TX frame | 2062 | `(1024+4+2)*2+2` 最坏 SLIP |
| B2 窗口1重传副本 | 1024 | 候选 payload，上层头/描述符另计 |
| B1-B3 描述符、状态及计数预留 | 1024 | 固定上限草案 |
| **B1-B3 小计** | **8206** | 约 8.02 KiB，不含任务栈和固件常量 |
| B4 Host 栈/TCB/NPL对象 | 8192 | 候选单 Host 执行上下文 |
| B4 mbuf/ACL/event 池及元数据 | 16384 | 必须由块数×对齐后块大小重新算实值 |
| B4 单连接/GATT/SM/存储及元数据 | 8192 | 固定服务、单连接、legacy advertising，关闭未用功能 |
| **B1-B4 合计草案** | **40974** | 约 40.01 KiB；另要求至少 16 KiB 未用 DTCM 余量 |

不能用 128 KiB 减栈就断言能容纳：没有当前 map/构建产物，完整 RW/ZI、调用栈、mbuf 开销和固件 Flash 占用均 NOT_VERIFIED。准入时必须提供同一工具链的基线/加入后 map，普通 RW/ZI+所有栈 <=112 KiB（草案门槛），固件只读镜像/流式分块，不把整份 Patch 复制到 DTCM；超预算先缩减功能或另审内存布局，不擅改 SDRAM/共享区。

## 8. B0.6 架构、风格与 B0.7 支线隔离

当前实际是 `CM7/Application` + `BSP/Platform/Service/Framework`。ApplicationV3 目录存在于旁边仓库 `codex/application-v3-flipper` 的已提交树，**不在当前 main/BLE 基线**。本轮仅交叉查看其树和资源，未把未提交代码视作稳定接口，未合并或复制。

后续候选职责（尚未冻结/创建）：

- `CM7/BSP/bsp_bt_uart.c/.h`：USART2、局部 GPIO/clock/IRQ、静态 ring、溢出与释放。HAL 若需要启用，参考现有 `bsp_*_hal_config.h` 局部边界方式，具体 uvprojx 变更单独评审。
- `CM7/Platform/bt_hci_h5.c/.h`：纯协议/transport 状态；不操作业务，不在 ISR 解析整包。
- `CM7/Service/ble_controller.c/.h`：版本、Patch、启动与恢复，显式状态机和首错诊断。
- `CM7/Service/ble_host_port.*`、独立 NPL/transport 适配：B4 才考虑。
- `CM7/ApplicationV3/Services/BLE`：仅 B6，且须先批准基线迁移；当前不存在，不能写出虚构 mailbox 调用。

沿用 SysResult_t、中文 `//` 注释、静态资源、显式状态、有界等待和失败路径；参考 `bsp_sdmmc.c`、`platform_sync.c`、`app_tasks.c`、`Common/Inc/sys_result.h`。已有源码存在非 UTF-8 文件，B0 不转码任何生产文件。

隔离边界：不修改 CM4、公共 FreeRTOSConfig、USB/Camera/SDMMC、Application 核心、链接/MPU 或 PLL。QSPI/Ethernet 在当前 HAL 配置未开启，不能据此宣称其他支线没有资源冲突；旁边 QSPI 支线为 `codex/qspi-flash`，仅查状态和候选文件，未完成全资源联合审查。USB_MODE_MSC 在 `usb_mode.c:329` 返回 NOT_SUPPORTED，枚举存在不表示 MSC 已实现。QSPI/Ethernet/MSC/ApplicationV3 后续合入时须对实际提交重新审计 GPIO、DMA/DMAMUX、IRQ、时钟组、缓存、任务优先级及容量。当前 B0 文档隔离 PASS 不代表未来功能共存 PASS。

## 9. B1-B3 测试设计与原始日志要求（均未执行）

| 阶段/用例 | 方法与可复核证据 | 预期判据 |
|---|---|---|
| B0-HW | DMM 连通；示波器电源/BT_DIS#/WAKE；照片记录模块料号；至少 10 次整板冷上电 | 电平/时序符合模块规格，有稳定 UART 输出或可解释静默；不以“无输出”证明不存在 |
| B1-UART | 合适电平 USB-UART 与逻辑分析仪；模块隔离方式先审核，避免两发送端争用；115200/even，递增序列/CRC，双向各 >=1 MiB | 应用字节一致、ORE/FE/PE/overflow=0；测 ISR 延迟、FIFO 与环形高水位 |
| B1-故障 | 注入 FE/PE、RX 超载、初始化失败和重复 init/deinit | 丢失可见、首错保留、环形索引不越界、释放不影响其他外设，恢复有界 |
| B2-纯协议 | 脱离 HAL 的主机向量：所有转义字节、坏头、CRC 正反例、超长/截断/粘包、seq7→0、重复包、非法 ACK、丢 ACK、重传和 peer SYNC | 正确包恰好一次交付，坏包不交付；重试耗尽失败；无越界/无无限队列 |
| B2-实板 | 冷启动捕获双方 SYNC/CONFIG/ACK 与 HCI event；双向 UART 原始字节留档 | ACTIVE 有完整协商证据；HCI opcode/status/长度匹配；不能仅展示状态变量 |
| B3-ID | 保存 0x1001/ROM/vendor 响应及候选文件 hash、选择规则 | 版本和 patch/config 精确匹配，未知版本拒绝 |
| B3-下载 | 每块 index/status/长度、终片、速率切换的前后时刻和成功后版本/能力 | 不丢块/不重复提交/不伪报 READY；HCI Reset/Read Version 可执行 |
| B3-恢复 | 坏 Patch、坏 config、错误 ACK、断线、半途失败、只复位 MCU、连续 10 次全板冷启动 | 报错有界并显式恢复/人工断电状态；自动恢复未实现不标 PASS |
| 联合运行 | 仅在获批固件上同时 UVC/SD 录像与 UART 压力；与基线对照 >=30 min | 视频/存储错误不增加，栈/池/队列有余量；尚未运行 |

原始日志字段：`session_id, baseline_commit, board_id, module_marking, fw_sha256, config_sha256, timestamp_us, epoch, direction, baud, parity, frame_type, seq, ack, reliable, crc_present, crc_ok, payload_len, retry, controller_state, opcode, status, result, uart_ore/fe/pe, ring_high_water, pool_free, stack_min_words`。二进制 RX/TX trace 与解码日志分别保存；累计计数不能替代原始字节。禁止在 UART ISR printf；建议内存固定 trace 后由探针/非阻塞路径导出。未采样字段填 null，不伪造 0。

建议工具：逻辑分析仪 ≥目标波特率10倍采样、示波器/DMM、调试探针、可限流电源和匹配 VDD_IO 的 USB-UART。未确认电平之前不能直接接 5 V 串口。纯协议测试在后续授权阶段编写；本次采集脚本不是协议测试。

## 10. 关闭条件与审核交接

| 问题 | 关闭证据 | 当前 |
|---|---|---|
| CTS/GPIO/电压 | 实板测量、模块 pinout/CTS 极性和指定 config 下双向 trace | NOT_VERIFIED |
| reset / 失败恢复 | 冷/热启动与下载中断后的有界恢复，或明确硬件改版与人工限制 | BLOCKED |
| 目标固件 | ROM/版本、来源/许可、二进制 hash、无流控/速率设置验证 | BLOCKED |
| H5 设计 | 审核偶校验、窗口、CRC、错误恢复、缓冲上限与测试向量设计 | NEEDS_FIX |
| NimBLE 无堆 | 全部分配点清单、固定容量方案、ARMCC5 编译/链接证明、耗尽测试 | NEEDS_FIX（B4 准入前必须关闭） |
| 基线与资源 | 审核继续 main 基线还是将来迁入 ApplicationV3；其他支线实际资源表 | NEEDS_FIX |
| 内存 | 当前及目标 map、池计算、最坏栈/IRQ 延迟和余量 | NOT_VERIFIED |

审核者可逐项给出 PASS / NEEDS_FIX / BLOCKED / NOT_VERIFIED。部分证据必须在后续受控实验才能取得；这不自动授权实现 B1。请先审核本报告与测试设计，由用户明确批准下一阶段或限定的诊断实验范围。本轮到此停止，不创建驱动、不加载固件、不移植 Host。
