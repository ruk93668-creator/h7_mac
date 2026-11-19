# RM0399 修订与 Endpoint 停止时序核验记录

> 本轮基线：`3a765feeee8d4cb3c1cc553337caf8cf1b63f6b4`；分支：`codex/usb-device-lifecycle-manager`；日期：2026-11-19（Asia/Shanghai）。
> 仅硬件证据与文档，不修改生产/测试/Keil/Core/HAL/LL，不实施P1-B/P2。
> VERIFIED须注明来源；INFERRED为推断；UNRESOLVED为缺证据；NOT_TESTED为未执行验证。

## 1. 当前结论

**UNRESOLVED**：未取得RM0399 Rev5完整正文。指定官方下载返回HTTP567、0字节，无PDF候选文件；浏览器获取受10MiB以上大小限制阻断。依据任务“官方错误/不完整则停止并报告”的要求，停止依赖正文的四类停止时序审计，未使用其他STM32系列手册、目录或转载预览替代。

**VERIFIED（过程）**：完成本机指定位置搜索及安装目录补搜，匹配文件总数0。没有可填写的RM PDF路径/页数/内容SHA256，不能用错误页或下载日志的SHA冒充手册SHA。本次没有实施任何停止寄存器序列。

## 2. 本机搜索清单

采用文件名 `RM0399*.pdf` / `DM00176879*.pdf`，大小写不敏感。搜索隐藏项目，跳过.git、symlink/reparse point；不进入禁止工程目录。访问错误0。未下载的云占位/跳过链接不构成已取得正文。

| 根目录 | 状态 | 匹配数 |
| -- | -- | -- |
| `D:/32/STM32H7` | VERIFIED：遍历允许的子树 | 0 |
| `C:/Users/29062/Downloads` | VERIFIED：实际已知文件夹路径一致 | 0 |
| `C:/Users/29062/Documents` | VERIFIED：实际已知文件夹路径一致 | 0 |
| `C:/Users/29062/Desktop` | VERIFIED：实际已知文件夹路径一致 | 0 |
| `C:/ST` | VERIFIED：不存在 | 不适用 |
| `C:/Program Files (x86)/STMicroelectronics` | VERIFIED：已搜索 | 0 |
| `C:/Users/29062/STM32Cube` | VERIFIED：已搜索 | 0 |
| `C:/32` | VERIFIED：安装注册记录指向，但目录不存在 | 不适用 |
| `D:/32` | VERIFIED：依据CubeMX安装位置补搜；已搜STM32H7子树不重复 | 0 |
| `D:/Users/29062/AppData/Local/Programs/STM32CubeMX` | VERIFIED：依据CubeMX安装记录补搜 | 0 |

**VERIFIED**：第一轮遍历140404个目录，0个访问错误。补搜`rg` exit1表示无匹配，stderr为空。安装注册记录为CubeMX6.12.0的C:/32、D:/32入口，以及6.13.0的D:/Users/...入口；注册信息不证明对应版本当前正在运行。没有发现其他可搜索的STM32CubeIDE/Programmer安装位置。

**VERIFIED（排除）**：始终排除 `D:/32/STM32H7/h7_mac_sd`、`D:/32/STM32H7/h7_mac`、`D:/32/STM32H7/h7_mac_wk`；没有操作原始工作区。0匹配意味着没有路径/大小/修改时间/SHA256行可列，不能虚构文件。

本机搜索原始清单：`D:/32/STM32H7/h7_mac_lifecycle/output/MSC24/udlm-hardware-evidence/local_search.json` 与 `installation_search.json`。

## 3. 指定官方地址获取结果

目标为RM0399 Rev5、October2026：[用户指定的ST官方URL](https://www.st.com/content/ccc/resource/technical/document/reference_manual/group0/82/40/1a/07/c9/16/40/35/DM00176879/files/DM00176879.pdf/jcr%3Acontent/translations/en.DM00176879.pdf)。

| 检查项 | 结果 |
| -- | -- |
| 下载记录时间 | VERIFIED：2026-11-19 17:28:51.835188 +08:00 |
| 工具 | VERIFIED：Windows curl，保留TLS校验，未绕过登录/证书警告 |
| HTTP / curl exit | VERIFIED：HTTP567 / exit22 |
| 实际下载量 | VERIFIED：0字节 |
| 候选文件 | VERIFIED：未创建；不是可解析PDF |
| PDF magic/首页RM编号 | NOT_TESTED：无文件 |
| Rev5/发布日期/页数/目录/末页与截断核验 | NOT_TESTED：无正文 |
| RM本机路径及SHA256 | UNRESOLVED：未取得，不能填写 |
| 下载记录本机路径 | `D:/32/STM32H7/h7_mac_lifecycle/output/MSC24/udlm-hardware-evidence/official_download_result.json` |

**VERIFIED（过程限制）**：本轮官方下载尝试与尚未结束的本机搜索发生了重叠；该尝试只留下失败记录，并未据此提前认定本机没有手册。最终“0个匹配”来自完成后的搜索与安装目录补搜。官方失败后未继续更换芯片手册或编写寄存器时序。

## 4. RM正文证据目录

以下全部 **UNRESOLVED**；不是对硬件语义的断言。取得原件后才可填具体章节、印刷页码、寄存器位定义及器件/实例映射。

| 待核验资源 | 所需证据 | 本轮状态 |
| -- | -- | -- |
| OTG_FS/OTG_HS与实例、寄存器映射 | USB2_OTG_FS的硬件身份、PIO/FS资源和寄存器有效范围 | 无RM正文，未核验 |
| DIEPCTLx、DOEPCTLx、DIEPINTx、DOEPINTx | EPENA、EPDIS、EPDISD、SNAK、CNAK、NAKSTS、XFRC及EP0特殊条件 | 无RM正文，未核验 |
| DCTL、DSTS、GINTSTS、GINTMSK、DAINT | global OUT NAK、有效中断/掩码、Reset、frame状态和退休 | 无RM正文，未核验 |
| GRSTCTL、GRXSTSP | FIFO选择字段FTXFNUM/TXFNUM命名及Flush前置条件/Pop语义 | 无RM正文，未核验，不擅定字段名 |
| GRXFSIZ、DIEPTXF、GNPTXSTS | 共享RX/各TX FIFO资源、剩余状态、与EP0共用的影响 | 无RM正文，未核验 |

**VERIFIED（源码事实，不是RM证明）**：当前使用USB2_OTG_FS/embedded PHY/FULL speed，USB DMA关闭（`CM7/BSP/bsp_usb.c:99`、`CM7/BSP/bsp_usb.c:105`、`CM7/BSP/bsp_usb.c:108`、`CM7/BSP/bsp_usb.c:111`）。HAL在RXFLVL路径读取GRXSTSP并写OUT buffer（`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1080`）；该源码行为不能代替手册规定的Pop/Flush安全条件。

## 5. Endpoint Hardware Stop Matrix

RM列不能编造页码；当前源码列为本基线的 **VERIFIED（源码）**，差距和实施结论为 **INFERRED / UNRESOLVED**。

| 项目 | RM0399依据 | 当前源码 | 差距 | 实施结论 |
| -- | -- | -- | -- | -- |
| Bulk IN | UNRESOLVED：章节/页码/位语义未核验 | `USB_EPStopXfer`：`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:915`；PIO续包`CM7/BSP/bsp_usb.c:655` | 写SNAK/EPDIS后只轮询EPENA，未形成完整事件/FIFO/PIO证明 | 不以EPENA0或HAL_OK释放source；暂不能批准停止算法 |
| Bulk OUT | UNRESOLVED：global OUT NAK与共享RX条件 | 同一LL函数；guard `CM7/BSP/bsp_usb.c:346` | 当前OUT与IN基本同形；other-RX guard不覆盖完整EP0资源 | 不假定IN/OUT同流程，不随意Flush共享RX |
| ISO IN | UNRESOLVED：PIO专用停止/帧时序 | `CM7/BSP/bsp_usb.c:520`、`CM7/BSP/bsp_usb.c:727` | 不完整通知/failed不能替代取消静止；DMA勘误不是PIO算法 | 不修改UVC，不据DMA限制生成PIO顺序 |
| EP0 | UNRESOLVED：SETUP抢占、状态包、特殊控制位 | `CM7/Platform/usb_device.c:1321`，`CM7/BSP/bsp_usb.c:287` | Close/Open与旧事件退休及共享RX需要完整硬件证明 | 不合并到普通非零EP Abort |
| TX FIFO Flush | UNRESOLVED：选择字段/前置条件/完成时序 | LL `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:452`；HAL wrapper `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:2018` | LL真返回被HAL丢弃；长迭代上限不等于短时间预算 | 接口层面可由BSP直接取LL返回，安全集成尚未证明 |
| RX FIFO Flush | UNRESOLVED：global NAK/共享RX/EP0与Pop | LL `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c:489`；BSP `CM7/BSP/bsp_usb.c:411` | 共享scope和新SETUP保护未闭合 | 不额外读取GRXSTSP来“清理”或诊断 |
| IRQ Retirement | UNRESOLVED：匹配事件语义、W1C及Reset | HAL EPDISD `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1144`、`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1234`；Stop `CM7/BSP/bsp_usb.c:188` | HAL无通用Bulk取消完成回调；BSP先关IRQ，失败直接返回 | 需要具名事件/PIO/所有权证明，不能仅清busy |

**VERIFIED（源码）**：HAL_PCD_EP_Abort直接返回LL结果（`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1992`）；LL轮询耗尽返回HAL_ERROR，不承诺稍后回调；EPDISD分支仅在ISO incomplete条件回调。HAL_EP_Flush丢弃底层返回。Platform在BSP成功后清busy（`CM7/Platform/usb_device.c:426`），现有结构仅opened/busy等字段（`CM7/Platform/usb_device.h:49`），不是完整静止证明。

## 6. 实施边界结论

- **UNRESOLVED**：本轮不能认证任一新的Endpoint停止操作已经安全，也不能提供硬件认可的Buffer释放条件。
- **INFERRED**：停止证明必须覆盖匹配身份、硬件静止、PIO引用、旧事件和所需FIFO处理；USB静止不等于SD IDMA静止。
- **VERIFIED（源码）**：公共LL接口确实返回Flush结果，源码上存在不改HAL/LL即可取得真返回的入口；不代表已经证明锁、scope、时序和时间上界。
- **UNRESOLVED**：BSP的取消/Close/Flush/IRQ顺序是候选修复边界；现阶段不能断言全部问题一定可不改HAL/LL解决，更不能先修改禁止模块。
- **UNRESOLVED（硬阻塞）**：RM0399完整正文仍缺。**尚不具备进入P1-B生产实现的硬件证据条件**。本轮没有开始P1-B/P2。
