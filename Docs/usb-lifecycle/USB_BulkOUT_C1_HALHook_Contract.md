# P1-C1 — 真实 HAL Hook 与硬件边界

VERIFIED（源码/本地资料）：基线 `3305d4a729e965247b33c95e5558eac576f1c6ef`；只扩展 HAL PCD、BSP 和 Platform。LL、USB Core、Class、Task 未改。RM0399 Rev5 原文件只读、未跟踪；3535页，SHA256 `61d07b67deb291aed8a5cf8019c94add0a1065318542e57b3287d2694b14bd60`。NOT_TESTED：本轮没有烧录或新的硅片测量。

## 手册依据与实现对应

VERIFIED（手册）：沿用 C0 审计的当前 USB2_OTG_FS、embedded PHY、Full-Speed、dma_enable=DISABLE，采用 Slave/PIO 条件；下表为 RM0399 Rev5 的 OTG core Device 寄存器/Device Slave 操作，不采用 Host 或 Buffer DMA 退休规则。页码为印刷页码。章节标题包含 OTG_HS，当前实例以 Full-Speed/Slave 功能子集使用，不能因此应用 HS DMA 条件。

| 功能 | 手册依据 | 当前接管路径 | 证据性质及边界 |
| --- | --- | --- | --- |
| 共享 RX FIFO | §60.11.2 p2764；表547 p2775 | HAL IRQ 的唯一 GRXSTSP pop；LL USB_ReadPacket | VERIFIED：status 和数据读取有消费副作用；BSP 不读第二次 GRXSTSP、不直接读 DFIFO |
| pop/copy 核验 | §60.14.8–11 p2792–2795 | `CM7/BSP/bsp_usb.c:696` / `CM7/BSP/bsp_usb.c:738` | VERIFIED源码：同一 status、不可变 cookie、copy 前核验 destination/容量/身份；copy 后只归还该 cookie 的 PIO 引用 |
| Global NAK 屏障 | §60.14.6 p2788；§60.14.41 p2827–2828；§60.15.6 p2909 | `CM7/BSP/bsp_usb.c:622`、`CM7/BSP/bsp_usb.c:673` | VERIFIED手册：SGONAK 后仍可到达先前数据；NAK marker 被 RX 消费才使 GONAKEFF 生效。INFERRED：单消费者水位可证明目标旧 DATA 已越过消费边界 |
| 非零 OUT Disable | §60.14.62 p2848–2849；§60.14.65 p2851–2853；§60.15.6 p2910 | `CM7/BSP/bsp_usb.c:910` | VERIFIED：有效 NAK 后 EPENA=1 才请求 EPDIS/SNAK；真实 EPDISD + W1C 退休 + EPENA/EPDIS 清零共同参与证明 |
| 解除 NAK | DCTL.CGONAK bit10、GONSTS bit3，p2827–2828、2910 | `CM7/BSP/bsp_usb.c:652` | VERIFIED源码：最后参与者离开后只发一次 CGONAK；真实 GONAKEFF/GONSTS 清除前保留 lease，提交不等于完成 |
| EP0 | §60.14.61 p2846–2847；p2828、2909 | guarded RX + 原有 Setup 消费 | VERIFIED：SETUP 可越过 Global NAK；从不对 EP0 使用 C1 Disable。冲突中的旧 EP0 owner 保留并进入故障，不假造通用 EP0 取消成功 |
| 终止事件 | DOEPINT XFRC[0]/EPDISD[1] W1C，p2848–2849 | `CM7/BSP/bsp_usb.c:760` / `CM7/BSP/bsp_usb.c:801` | VERIFIED源码：W1C 前保存整轮事实；XFRC 和 EPDISD 同轮只裁决一次；不把 EPDISD 自动变为 CGONAK |
| FIFO Flush | §60.14.5 p2784；Reset 级全 OUT 流程 p2910–2911 | C1 不发 RXFFLSH；`CM7/BSP/bsp_usb.c:1387` | VERIFIED源码：单 EP RX Flush 在绑定模式下拒绝；HAL Reset 的全控制器行为不作为旧 Buffer 已归还的证明 |

UNRESOLVED（手册编辑）：p2794–2795 相邻 Host/Device 标题/布局容易混淆；继续以 p2793 Device 字段和 CMSIS/HAL 位定义交叉核验，不修改手册。NOT_TESTED：上述软硬件桥接在真实 OTG core 上的延迟、极端总线时序及实际 GSNPSID。

## 成套绑定与默认行为

VERIFIED：`Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_pcd.h:163` 接受完整 Version1 表，任一必需 Hook 缺失则拒绝，DMA 模式拒绝；调用者须串行化绑定，BSP 在短原子窗口内发布，当前表为静态常量。GAHBCFG.GINT 关闭、Task 上下文、PCD READY 才允许绑定；替换/解除还须 CanDetach 证明无引用。

VERIFIED：`CM7/BSP/bsp_usb.c:594` 进一步限定为成功的全新 BSP Init 后、首次 Start 前且非零 OUT 未 Open。Start 尝试即关闭首次绑定窗口。不可把普通 Stop 后的旧硬件历史追认为新命名空间。已绑定的幂等调用不创建新 Controller 身份。原 Init 检查 USB_CoreInit、USB_DevInit 返回值，失败不能开放窗口。

VERIFIED：`CM7/Platform/usb_device.c:2207` 还要求 Class 未活动、没有借出 Class 内存、控制事务未占用。原有三配置没有自动调用此接口；HAL Lifecycle=NULL 时保留旧 RX、NAK、ISO、W1C 行为。C0 原24项在未绑定路径单独回归。没有自动迁移 MSC BOT 或 UVC 生命周期。

## Hook 顺序、所有权与有界性

VERIFIED：`Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c:1050` 的 managed RX 路径保持一次 GRXSTSP pop → Boundary → RxBegin → 原 LL copy/定长 discard → RxEnd。BCNT 11位；拒绝 copy 时按该 status 的字数消费到栈上4字节槽，最多512次，不写被拒绝 Buffer、不 Flush、不再 pop。USB_ReadPacket 原实现只写实际 len 字节，尾字从 FIFO 读一次并逐字节写目标；LL 未改。

VERIFIED：允许的 Buffer 为 CM7 可写内部 DTCM/AXI/D2/D3 SRAM，校验溢出、原始 capacity、received、maxpacket、当前 HAL pointer/count 及 Session。Flash、外部 SDRAM 和未知地址拒绝；这是有意缩小的接口范围。PIO 使用 DTCM 不表示 USB DMA 可用 DTCM。调用者仍须排除其他 CPU/DMA 对同一 Buffer 的访问。

VERIFIED：原 HAL ISO OUT incomplete 请求、BOUTNAKEFF 调度及任意 OUT EPDISD 自动 CGONAK 三处均在绑定模式被完整接管。ISO 通过同一 lease 参加并等待真实 Disable 事件；无主 NAK 或已开始解除的 lease 不可被 ISO 偷取。IN ISO 分支未迁移。

VERIFIED：Reset/Disconnect 在首次 RX/callback 前通知旧 Session 失效；在 copy/终止事件后再次核验边界。资格失效只设置 fence/故障并保留 source、PIO 和 lease。IRQ Hook 没有停止轮询；循环只受 endpoint 数或 BCNT 固定界限约束。Task Poll 每次推进一阶段，短 PRIMASK 发布窗口不跨轮等待，也不关闭整个 USB IRQ 等硬件。

VERIFIED：正常 XFRC 若仍有 RXFLVL，保留原终止 cookie 和 Buffer，W1C 后暂缓业务回调；`CM7/BSP/bsp_usb.c:831` 仅在真实 RX 消费完成、PIO=0、终止位退休、目标 disabled 后交付原完成。Stop 可接管此事件而不再交付普通完成，普通回调可重新 Arm，新身份不被旧 EventEnd 清理。

VERIFIED（当前构建配置）：`CM7/Core/Inc/stm32h7xx_hal_conf.h:206` 的 `USE_HAL_PCD_REGISTER_CALLBACKS=0U`，三配置使用此设置。NOT_TESTED：动态注册回调模式、其他控制器实例、USB DMA 模式的成套接入；本轮不将当前 BSP 的固定回调所有权结论扩展到这些配置。
