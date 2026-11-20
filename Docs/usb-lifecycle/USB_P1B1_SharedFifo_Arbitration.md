# P1-B.1 共享 GRSTCTL / FIFO 仲裁

## 共享命令访问表

VERIFIED（源码），下表源码前缀 `Drivers/STM32H7xx_HAL_Driver/Src/`，并非只扫描函数名：

| 路径 | 寄存器访问 / 调用 | 上下文与本轮处理 |
| --- | --- | --- |
| stm32h7xx_ll_usb.c:452 / :489 | USB_FlushTxFifo :469 写 TXFFLSH/TXFNUM；USB_FlushRxFifo :506 写 RXFFLSH，循环等待清位 | 公共 LL 真实返回值；不等于 HAL wrapper 的返回值 |
| stm32h7xx_ll_usb.c:300 / :352–357 | USB_DevInit 初始化 Flush 全 TX/RX | Init，不与已持有 Bulk 停止引用并行 |
| stm32h7xx_ll_usb.c:1381–1412 | USB_CoreReset 的 AHBIDL/CSRST 写与轮询 | Core Init 路径，不能作为在途资源恢复手段 |
| stm32h7xx_ll_usb.c:1112 / :1131–1137 | USB_StopDevice Flush RX/全 TX | HAL DeInit；旧路径不是本轮完整证明 |
| stm32h7xx_hal_pcd.c:1021 / :1029 | HAL_PCD_Stop Flush 全 TX 丢弃 LL 返回值 | BSP Stop 仅在 Bulk 引用已排空后可进入；其他类型仍旧契约 |
| stm32h7xx_hal_pcd.c:1234–1251 | IN EPDISD 分支对该 IN Flush，忽略返回 | IRQ，所有 IN 端点均可能发生，不能按 IEPINT 类别排除 |
| stm32h7xx_hal_pcd.c:1332–1397 | USBRST 全 TX Flush、端点/地址/EP0重置 | IRQ，必须处理；先失效/保留旧 Proof |
| stm32h7xx_hal_pcd.c:2018–2033 | HAL_PCD_EP_Flush 调 LL 后返回 HAL_OK | BSP Close/Abort/AbortAll/Stop/FlushEp；有旧 HAL 隐藏失败的边界 |
| stm32h7xx_ll_usb.c:1425 / :1461–1466；:2151 / :2162–2167 | USB_HostInit、USB_StopHost 全 TX/RX Flush | 本设备 PIO 模式无 Host 调用；不能推广为 Host 并发也安全 |
| CM7/BSP/bsp_usb.c:277–367 | Bulk IN Poll 专属命令写、TXFNUM与清位核验 | Task，短原子窗口发命令；跨 Poll 不屏蔽全 USB IRQ |

VERIFIED：BSP 中 Stop、CloseEp、AbortEp、AbortAll、FlushEp 入口检查 Bulk FIFO owner；新命令只在 AHBIDL、无 TX/RX Flush、owner=0 时发出。原 Task 所有者串行使用这些接口（app_usb.h:37 的 Task 限定）。不支持其他 Task 绕过 Platform 直接并发调用 HAL/LL；没有新增跨 Task mutex，也不宣称此扩展场景已覆盖。

## 为什么不能简单放过 SOF 或 XFRC IRQ

VERIFIED：HAL_PCD_IRQHandler 的多条 `__HAL_PCD_GET_FLAG` 在处理过程中重读 GINTSTS。即使入口只有 SOF，调用完整 HAL 处理器期间新 Reset/EPDISD 到达仍可能 Flush。非目标 IN 正常 XFRC 会调用 Class，且同一个 endpoint interrupt word 可以同时含 EPDISD。单看 SOF 或 IEPINT 类别不足以排除冲突。

VERIFIED：`CM7/BSP/bsp_usb.c:1071` 只在 FIFO owner 非零、完整 raw GINTSTS 恰为 SOF 且 SOF mask 有效时使用受限 SOF 分支：取 FrameNumber，调用本 BSP SOF 回调，仅 W1C SOF 后返回，不进入 HAL 其他分支。新到达的其他事件保持 pending。没有任意弹出 GRXSTSP，没有长期关 IRQ。

VERIFIED（源码）：当前 `USE_HAL_PCD_REGISTER_CALLBACKS=0`，配置在 `CM7/Core/Inc/stm32h7xx_hal_conf.h:206`。BSP SOF→Platform Usb_Sof→Core USBD_LL_SOF→Class SOF；UVC_Sof（usb_uvc_class.c:1077）仅记录 slot，MSC/TEST 无硬件 Flush SOF 实现。其后的通知只唤醒同一个 Task。此证明依赖当前回调契约，未来添加可写 GRSTCTL 的 SOF 回调必须重新审核。

VERIFIED：其他事件组合仍先 taint FIFO Proof，再进入 HAL 处理；Reset、Disconnect 不被吞掉。故障保留 owner、Buffer、fence。完成位即使清除也不能成为原操作的无冲突完成证明。新增模型覆盖 SOF 途中迟到 Reset：SOF W1C 不清 Reset，下一次 IRQ 使旧 Proof 失效。

UNRESOLVED：本轮只关闭纯 SOF 的不必要冲突，非目标 XFRC、EPDISD 等组合仍保守 RECOVERY_REQUIRED；没有宣称任意 IRQ/负载都能无故障停止。要继续减少这类保守失败，需要更细的 HAL 事件分发/命令参与协议，当前禁止修改 HAL/Class，停止相应扩展而非放宽证明。

## PIO / DMA 与 ES0445 边界

VERIFIED：`CM7/BSP/bsp_usb.c:504` 的 USB DMA 为 DISABLE，实际是 Slave/PIO；端点 USB 证明不能替代 SDMMC IDMA 证明。

VERIFIED（官方勘误）：[ES0445 Rev7](https://www.st.com/resource/en/errata_sheet/es0445-stm32h745xig-stm32h755xi-stm32h747xig-stm32h757xi-device-errata-stmicroelectronics.pdf) 第1页 Table2 将 REV_ID=0x2003 归入 U/V。第43页2.24.2限 Buffer DMA ISO fetch；不能直接套用到本 PIO。第44页2.24.5覆盖设备 ZLP 的 Slave 模式，要求分开的 NAK、EPENA、CNAK 顺序及20/15 AHB周期等待。

VERIFIED（源码）：LL `USB_EPStartXfer` :743 的零长度分支设置 XFRSIZ=0/PKTCNT=1，Slave :818 同时 CNAK|EPENA，缺上述序列。新增用例为源码边界/适用性模型，不是硅片测试，也不是所有 ZLP 调用链全覆盖。

UNRESOLVED / 扩展申请：需另行批准覆盖全部 IN ZLP（含 EP0 status）的底层提交接口与时钟有界延时契约，或 HAL/LL 受审核扩展。不得只在 Bulk 停止接口处理、宣称全 ZLP 已修复。本轮未改 HAL、LL、Core 或 EP0 协议实现，未烧录。

RM0399 Rev5 参考：§60.14.5 p2784 的 GRSTCTL；§60.14.55–56 p2840–2843 的 INCTL/ININT；§60.15.6 p2917–2918 的普通 IN 停止与 FIFO 时序。PDF 原地只读保留、不纳入 Git。硬件实际时序 NOT_TESTED。
