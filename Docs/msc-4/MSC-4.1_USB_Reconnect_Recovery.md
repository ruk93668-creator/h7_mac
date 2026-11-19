# MSC-4.1 USB Disconnect / Reconnect 生命周期故障定位

日期：2026-11-19（Asia/Shanghai）。任务基线：`8691497062f1459592decd7ea154c4fc6151c1c9`，分支 `codex/usb-msc-bringup`。开始时本地与 origin 分支 SHA 一致。

**当前为诊断进行中，根因 UNRESOLVED；尚未实现恢复修复，不宣称重连验收通过。** VERIFIED 指源码/已有快照/指定测试确认的事实；INFERRED 指推断；UNRESOLVED 指尚待定位；NOT_TESTED 指未执行。

## 1. 现有证据与边界

VERIFIED：MSC-4 真实 SD 读取及文件复制成功。用户确认读取中拔线后，不复位重接未恢复 F:，Windows 报 Device Descriptor Request Failed；这轮操作没有抓包。既有 SRAM 快照保留，不重新标成完整 USB 时序证据。

VERIFIED（本轮按原 ELF 离线补充解码）：重连快照中 Core 已收到 `80 06 00 01 00 00 40 00`（Device Descriptor，wLength=64）。Core ep0_state=DATA_IN；BSP EP0 TX active=1、total=18、offset=18，Platform EP0 IN busy=1。表明 SETUP 已交付并提交18字节描述符，不支持“重连后完全收不到SETUP”的简单结论。PCD IN0 xfer_count=18是HAL软件传输计数，不能当作主机ACK证据。

VERIFIED：同一快照 Core dev_address 与 PCD USB_Address 均保留11，而 Core dev_state=DEFAULT、dev_config=0。硬件 DCFG 未在旧快照中采样，因此地址残留是否参与故障为 UNRESOLVED。HAL USBRST 已清硬件 DAD，不能仅凭软件值11判断硬件地址错误。

VERIFIED：旧写入口探针 `0x08010840` 的 BL 列表仍为 enabled，用户本轮确认已禁用。后续 ELF 布局会变化，禁止沿用旧地址探针。此信息也限制旧探针会话的可靠性，不直接证明旧故障由探针造成。

## 2. 实际调用链审计

路径均相对仓库根目录；行号为本轮加入可选诊断时的源码位置，后续功能补丁可能移动。

| 入口 | 实际路径/函数与调用关系 | 核对结论 |
| --- | --- | --- |
| 初始化 | `CM7/Platform/usb_device.c:299 USB_Device_Init -> Usb_InitDevice -> USBD_Init -> USBD_LL_Init:558 -> BSP_USB_Init/SetCallbacks` | VERIFIED：Init不连接主机；BSP配置PCD与FIFO，清NVIC pending |
| Start | `USB_Device_Start:350 -> Usb_StartDevice:1698 -> USBD_Start -> USBD_LL_Start:613 -> BSP_USB_Start` | VERIFIED：Platform/BSP均拒绝重复Start；BSP清NVIC pending后HAL_PCD_Start，最后开放IRQ |
| Stop | `USB_Device_Stop:370 -> Usb_StopDevice:1729 -> BSP_USB_Stop -> HAL_PCD_EP_Abort/Flush -> HAL_PCD_Stop` | VERIFIED：先确认硬件停，再释放Class及数据端点；不会直接假定USBD_Stop已传播底层错误 |
| DeInit | `USBD_LL_DeInit:573 -> BSP_USB_DeInit -> BSP_USB_Stop -> HAL_PCD_DeInit -> HAL_PCD_MspDeInit` | VERIFIED：Class资源未释放或硬件停失败时拒绝；MspDeInit禁用/清NVIC并关USB时钟 |
| USB IRQ | `CM7/BSP/bsp_usb.c:619 OTG_FS_IRQHandler -> HAL_PCD_IRQHandler` | VERIFIED：BSP ready/started才分发；USB DMA关闭，PIO由CPU操作FIFO |
| USBRST | `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1332` | VERIFIED：清端点旧中断/硬件STALL，清DCFG.DAD，USB_EP0_OutStart，W1C USBRST；此分支不调用Platform Reset |
| ENUMDNE / Reset回调 | HAL `:1381 -> HAL_PCD_ResetCallback`；BSP `:685 -> s_callbacks->reset -> Usb_Reset:1481 -> USBD_LL_Reset` | VERIFIED：HAL ResetCallback实际在ENUMDNE分支；不能将事件记录里的Reset回调等同USBRST发生瞬间 |
| Core Reset | `Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c:771 USBD_LL_Reset` | VERIFIED：先Class DeInit，再OpenEP0 OUT/IN；忽略Open返回值，Platform另核对两方向opened及释放结果 |
| SETUP | HAL RX FIFO/OUT STUP -> BSP `HAL_PCD_SetupStageCallback:626 -> Usb_Setup:1321 -> USBD_LL_SetupStage` | VERIFIED：新SETUP先失效旧Token；旧busy EP0须Close/Open确认停止；失败锁存stop_required，拒绝新事务 |
| 地址 | Core SET_ADDRESS -> `USBD_LL_SetUSBAddress:848 -> BSP_USB_SetAddress -> HAL_PCD_SetAddress` | VERIFIED：显式SetAddress更新PCD软件地址及硬件；USBRST直接清硬件地址不会调用此包装入口 |
| Connect | BSP `HAL_PCD_ConnectCallback:706 -> Usb_Connect:1533 -> USBD_LL_DevConnected` | VERIFIED：连接回调不等于枚举完成，不重复Start |
| Disconnect | BSP `HAL_PCD_DisconnectCallback:713 -> Usb_Disconnect:1543 -> USBD_LL_DevDisconnected:995 -> ManagedClassDeInit -> Msc_DeInit/BotStop` | VERIFIED：失效Token，释放Class并尝试清数据端点；错误锁存；不自动交还RAW给FatFS |

### EP0、Token、IRQ与缓冲

VERIFIED：`Usb_InvalidateControl` 只取消旧请求资格，不清Token seed，不释放外部IO。新SETUP、Reset、Disconnect、Stop、DeInit均调用；Token耗尽拒绝分配。`CompleteControlStatus`要求当前Token、WAITING、任务上下文及Configured；真实EP0 IN完成回调才把SUBMITTED变COMPLETE，提交不等于主机确认。

VERIFIED：`BSP_USB_CloseEp` 对EP0屏蔽本方向，Abort成功后只清本方向旧完成事件；IN清TX0，不随意清共享RX；不把SETUP W1C当作旧传输完成。数据端点另有Abort/Flush，未停止前不归还缓冲。

VERIFIED：BSP在Init/Start/Stop/DeInit相关路径清NVIC pending；HAL USBRST清设备端点中断。Platform没有任意清所有IRQ/状态的恢复捷径。物理拔线不一定触发软件Disconnect：BSP `vbus_sensing_enable=DISABLE`；需新抓包、回调序列与硬件寄存器核对，不能直接定为根因或擅自开启VBUS。

UNRESOLVED：旧完成与重连新EP0的实际时序、PCD软件状态与硬件寄存器差异、失败时IN0的使能/中断状态。不能靠全局memset、强制Stop/Start或清IRQ掩盖。

## 3. 当前诊断改动（尚无恢复逻辑修复）

仅改 `CM7/Platform/usb_device.c/.h`，新增 `USB_DEVICE_LIFECYCLE_DIAGNOSTICS`，默认0。诊断固件显式设1，记录最近128条事件：Reset前后、Disconnect前后、Connect、SETUP前后、EP0 IN/OUT/错误、地址提交、EP0 Open/Close、Start/Stop/DeInit。

每条含顺序号、请求类型/长度摘要、Token/状态、Core状态/地址、EP0占用标志及最近错误。不存USB或SD数据。序号仅用于取证，耗尽停止记录，不充当协议Token。写日志时短暂屏蔽USB IRQ，无格式化输出、动态分配或停机断点。会改变诊断固件时序，最终还需关闭诊断的修复固件回归。

`Tests/MscReconnect/prepare_debug.py` 按精确ELF DWARF准备只读SAVE脚本，保存SRAM及指定安全USB状态寄存器，避开会弹出RX状态的GRXSTSP和FIFO。PCD完整状态由DWARF解析；硬件DCFG与软件地址分别记录。

## 4. 构建与软件测试

VERIFIED：原Deferred Status 17组断言在诊断关闭/开启两种模式均通过；旧断言未改，结果分别见 `Tests/MscReconnect/platform_0_results.json`、`platform_1_results.json`。这些使用模拟HAL，不能替代实板事务/IRQ/DMA验证。

VERIFIED：首次临时RAM构建失败，8个未定义BOT/RAM符号，0警告。当前用户Keil工程只列入MSC Class和Descriptor，缺少5个现有实现源文件。没有修改用户工程；仅在临时工程补入 `usb_msc_bot.c / usb_msc_scsi.c / usb_msc_ram.c / usb_msc_storage.c / usb_msc_sd.c`。该构建输入问题与旧重连故障的因果关系未建立。

VERIFIED：三组诊断固件完整CM7构建均为0 Error / 0 Warning，编译器Arm Compiler 5.06 update7 build960。每个构建使用独立临时工程，明确 `USB_MSC_SD_WRITE_ENABLE=0`，结束恢复RTE并核对用户工程/options原字节；构建过程中RTE临时变化不作为最终修改交付。完整命令及预处理结果见 `Tests/MscReconnect/build_results.json`，脱敏构建日志为同目录三个 `*_build.txt`。

| 诊断固件 | SHA256 |
| --- | --- |
| RAM MSC | `d9457c46a9581a4a9ff8c4d984dd09865c906bc490b3424a3f53d2676db8bd5c` |
| SD只读MSC | `ad6e75aaa56db5d2542fbad5eb325eeaea6c678da5b2b1bcf48000e723fd6873` |
| 默认UVC | `4cf81f47cdbb94a6caccdf3eab78885a741d89c05a5354df982b9c37df0ffec3` |

本地固件在 `output/MSC41/diagnostic-build/{ram,sd_readonly,uvc}/`，每组提供同哈希 `h7_mac_CM7.axf` 别名供用户现有下载路径使用。未覆盖不同内容的文件。

VERIFIED：10个历史UVC/Core测试入口在基线与当前诊断默认关闭源码上均执行。两侧Ep0IrqRace入口通过，其他入口仍返回失败，不能报全绿。逐用例对照的9份结果无新增失败/缺失/新增用例：Streaming35→35、Payload6→2、Ep1Lifecycle45→45、ProbeCommit89→89、Cancellation39→39、CoreDecoupling3→3、Lifecycle6→6、字符串0→0、Cleanup14→14。Payload失败数量差异原因未定位，不归功于本轮诊断。旧断言未改，详见 `legacy_case_comparison.json` 及两侧回归摘要；原始详细日志留在本地 `output/MSC41/legacy/`。

## 5. 三组实板对照与修复验收

| 组别 | 首次枚举/抓包 | 断线重连/抓包 | PCD/EP0/事件快照 |
| --- | --- | --- | --- |
| A 默认UVC诊断 | NOT_TESTED | NOT_TESTED | NOT_TESTED |
| B RAM MSC诊断 | VERIFIED：Windows OK，8KiB RAM，Configuration=1 | VERIFIED：未复位重连失败，Windows设备描述符请求失败 | VERIFIED：首次与重连SRAM/USB寄存器均已保存 |
| C SD只读MSC诊断 | NOT_TESTED | NOT_TESTED | NOT_TESTED |

同一电脑、端口、线缆。每次连接前启动捕获；固件运行后再接数据线，避免Keil暂停本身使Windows超时。初次稳定后保存快照，恢复运行再拔线/重接；先保存失败状态，不提前复位。若UVC因既有Camera初始化问题无法启动USB，应记为受阻，禁止改Camera路径来制造通过。

2026-11-19，用户已确认关闭全部供电后取出SD卡，并烧录运行RAM诊断固件，正在准备B组首次枚举捕获。A/B组不插SD，避免默认本地Storage路径接触真实卡；C组切换固件与装回卡时再断电。RAM组出现容量很小或要求格式化时一律取消，不执行格式化。

### B组首次实板对照（2026-11-19补充）

VERIFIED：用户确认SD已取出、旧探针禁用；RAM镜像哈希与上表一致。首次枚举Windows设备/磁盘状态OK，物理盘容量8192字节；首次快照硬件DCFG地址=21、PCD USB_Address=21、Core dev_address=21，Core Configured/config1，BOT_CBW/epoch1/pending0。

VERIFIED：首次快照后恢复Run，用户按要求只拔USB数据线、等待5秒、同口重接，不复位，等待后Break保存重连快照。Windows报告未知USB设备（设备描述符请求失败）。硬件DCFG地址=0，Core DEFAULT/config0/EP0_IDLE；PCD与Core软件地址仍21。硬件地址确实归零，不能把软件残留地址21直接当作失败根因。

VERIFIED：重连现场 `stop_required=1`、`last_result=-9`、Class=RELEASE_FAILED，Platform与BSP started仍1。IN2已关闭；OUT2仍opened/busy，maxpacket64、last_result=-9，BSP rx_active[2]=1。补充寄存器 `DOEPCTL2=0xC00A0040`：EPENA=1、EPDIS=1、NAKSTS=1；USB IRQ101的NVIC ISER3 bit5=0。PCD READY/UNLOCKED/ErrorCode0不代表端点取消已完成。

VERIFIED（源码与现场吻合）：数据OUT2经 `BSP_USB_AbortEp -> HAL_PCD_EP_Abort -> USB_EPStopXfer` 等待EPENA清零；LL在10000次轮询后返回HAL_ERROR，BSP映射SYS_ERR_IO(-9)。`BSP_USB_Stop:195`先关闭NVIC，在Abort失败分支直接返回，只有后续所有Abort/Flush成功才到HAL_PCD_Stop与状态归还。故障现场不能把s_started仍1解释为USB IRQ仍工作；IRQ关闭后无法继续软件处理主机SETUP。

INFERRED：上述链说明持续不响应的直接阻塞位于BSP取消/停止契约；它不依赖SD读写。**UNRESOLVED：首次OUT取消未完成的具体原因、触发时刻及完整Reset/Disconnect回调先后**。128条事件日志被重复STOP覆盖，当前保留下来的128条全为STOP，不能伪造首次故障调用时序。Token始终0，无本轮Deferred Status请求参与证据；不能将此推广成所有Token竞态已经通过实板验证。

VERIFIED：原始USBPcap `output/MSC41/ram-reconnect-01.pcapng`，16893188字节、123708包，时间13:10:32.556275～13:14:50.633099（Asia/Shanghai），SHA256 `ec7f8963ddcb346daec29d9dc237cd4b612a8d84cfa42c8ff089c6fa9c8b852a`。首次Device请求/响应frame6096/6097、配置frame6098～6101、SET_CONFIGURATION6104/6105、GET_MAX_LUN6108/6109成功。此捕获未解析出SET_ADDRESS事务，不能补写为抓包已验证；首次地址21由MCU快照与Windows设备身份佐证。原始整Hub流量留本地，不上传Git。

补充证据：`Tests/MscReconnect/results/` 下的两份解码快照、Windows状态、额外端点/NVIC寄存器和捕获摘要。原始SAVE文件保留MDK目录，并复制到 `output/MSC41/snapshots/{ram_initial,ram_reconnect}/`。不移动、不删除现场文件。

按用户“如果需要超出授权范围，停止并提交审批方案”的要求，已停止恢复修复与后续实板操作；未修改BSP/ST Core/HAL。待审方案见同目录 `MSC-4.1_BSP_Lifecycle_Change_Approval.md`。A/C对照、修复与10次回归尚未完成，不把B组取证当作整体验收。

修复后的冷启动、正常重连、连续10次、USB Reset、BOT Reset/ClearHALT、读取中断线、迟到IRQ、重新读取SD与默认UVC实板回归均为NOT_TESTED。根因未确认前不预填修复结论；若需修改BSP或其他未授权模块，停止并单独提交审批方案。

## 6. 数据安全、Git与后续

SD仍只读。禁止真实SD写入、格式化或本地FatFS占有。Token取消不等于SDMMC静止，需独立检查IDMA/Worker；RAW租约不因USB事件转交。原有用户 `h7_mac.uvprojx/.uvoptx` 及全部旧快照、抓包保留。

当前诊断尚未完成，不宣称任务已修复或验收通过。后续提交/推送及SHA核对在完成对应检查后记录；不合并main，不进入MSC-5。
