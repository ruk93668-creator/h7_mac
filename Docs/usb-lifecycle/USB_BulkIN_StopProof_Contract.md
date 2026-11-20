# P1-B Bulk IN Stop Proof契约

> P1-B基线：`5a27af621c219ead2bc680858efd4aeb17e110a4`；工作区：`D:/32/STM32H7/h7_mac_lifecycle`；分支：`codex/usb-device-lifecycle-manager`。
> VERIFIED注明源码/手册/软件模型；INFERRED为设计推导；UNRESOLVED为尚缺证明；NOT_TESTED为未执行的实板验证。本文不批准P1-C、EP0/UVC/MSC迁移或统一生命周期管理器。

## 1. 硬件及软件前置条件

**VERIFIED（手册）**：RM0399 Rev5 2918页要求先停应用AHB写入，再SNAK/INEPNE、EPDIS/EPDISD及定向TX Flush。2784页要求Flush前无FIFO读写，AHBIDL及NAK有效；TXFNUM在命令完成前不可改变。2843页区分INEPNE、EPDISD、XFRC。EPENA=0本身不是完整取消证明。

**VERIFIED（源码）**：新路径用目标DAINT/TXFE隔离、fence和同一CM7串行调用证明旧PIO无法继续。BSP复制由pio_users计数；HAL FIFO读取只能在目标IRQ路由开放时执行。source/pool直到Commit仍保留，硬件不直接DMA访问源Buffer。BSP结构和外部Class、SDMMC的引用互相独立。

## 2. Proof中保存的事实

| 字段 | 含义与边界 |
| -- | -- |
| transfer / stop_attempt | VERIFIED：完整Transfer身份和独立StopAttempt，错误/耗尽均拒绝，不回绕 |
| initial_ctl / initial_events / saved masks | VERIFIED：Begin时原始CTL/INT及仅目标bit的旧mask；W1C前保存 |
| events / packet_completions | VERIFIED：观察到的事件集合与当前包唯一结算次数；不把mask变更当退休 |
| queued/completed/current_packet | VERIFIED：提交进度、XFRC完成进度、当前包长；错误状态不作为成功传输字节数 |
| source_owned / pool_owned / pio_users | VERIFIED：应用源引用、PIO槽预留、正在进行的BSP复制；不是allocator free或SD停止 |
| nak_effective / disable_observed | VERIFIED：NAK已生效、禁用事件曾观察；无任一单字段放行 |
| fifo_complete / fifo_result | VERIFIED：本次BSP逐步Flush结果，SYS_ERR_BUSY/真实超时/成功；不是伪造HAL返回值 |
| events_retired / completed_undelivered | VERIFIED：终态事件已退休；全部字节已完成但没有普通Class交付。后者不独立授权释放 |
| state / stage / result / error | VERIFIED：内部状态、阶段、进度和原始工程错误。必须联合检查 |
| source_returns / pool_returns | VERIFIED：当前Transfer内的引用归还计数，正常完成或Commit恰好一次 |

## 3. 可接受路径

**VERIFIED（源码/软件模型）**：

- 尚未Arm且没有源/槽引用、EPENA=0、无旧终态事件：空闲证明，可Commit结束fence。
- 普通回调已处理最后XFRC并归还引用：ALREADY_COMPLETED，不再归还一次，Commit只结束停止事务。
- 未交付XFRC：当前包结算一次；即便最后包已完成，也不调用普通Class回调。中间包不得继续提交。
- 活跃取消：NAK有效→仅在EPENA=1时EPDIS→匹配禁用及EPENA/EPDIS清零→AHB idle→目标FIFO Flush位清零→保存并W1C终态→核对退休→PROVEN。
- XFRC在禁用前先使当前包结束：不等待从未请求的EPDISD；仍需NAK/FIFO/引用/事件证明。
- 未由本attempt发起的EPDISD、EPENA=0却无当前终态证据：进入RECOVERY_REQUIRED。

## 4. 事件及FIFO要求

**VERIFIED**：终态位集合为XFRC/EPDISD/TOC；INEPNE、NAK、TXFE等live条件不等同旧Transfer完成。只W1C本事务拥有的终态位，保存其余观察。Commit检查当前公共mask是否会暴露未退休的相关事件；不会擅改公共mask。新Arm前有旧终态位则拒绝，不能把它分配给新Source。

**VERIFIED**：不调用HAL_PCD_EP_Flush作为成功证明。公开USB_FlushTxFifo虽返回真实状态，但其内部巨大迭代上限不可被外层deadline中断，因此本实现按已核验RM序列在BSP执行单步寄存器进度，保存工程结果。只允许与端点对应的已规划TXFNUM，不Flush所有TX，也不碰RX FIFO或GRXSTSP。

**UNRESOLVED**：其他USB IRQ在共享Flush窗口触发保守故障；自动Reconcile不实现。在证明前不能清fifo owner、恢复fence或复用缓冲区。最终硬件延迟边界/跨新Arm的非预期旧硬件事件必须在后续实板中验证；软件身份本身没有硬件标签能力。

## 5. Commit谓词及拒绝行为

**INFERRED（契约，源码按此检查）**：

```text
release_BSP_reference = identity_matches && attempt_matches && stage == PROVEN
                     && session_still_valid && no_bus_reset_pending
                     && fifo_complete && events_retired && pio_users == 0
                     && EPENA == 0 && no_old_terminal_status
                     && no_related_unretired_event_exposed_by_restored_mask
```

**VERIFIED**：成功后才清PCD旧指针/长度与本Transfer元数据，归还source/pool并恢复仅目标原mask；Platform在已有序号匹配后清busy。失败保留真实引用；重复Commit幂等；旧身份对新传输无权。恢复CPU原PRIMASK，不无条件EnableIRQ。

**UNRESOLVED**：这个谓词只证明BSP对USB源引用的归还资格，不包含Class逻辑、Storage Worker、SD IDMA或FatFS访问权；RAW租约不因任何USB事件自动归还。

## 6. 时间与失败结果

**VERIFIED**：每次Poll没有内部等待循环，累计deadline依赖现有HAL_GetTick。NAK/Disable/AHB/FIFO/访客未满足均PENDING；到期保持引用并RECOVERY_REQUIRED。硬件标称8慢时钟不当故障上限。参数/身份错误直接返回错误，不覆盖旧私有Proof。失败状态从不强制清busy。

**NOT_TESTED**：真实临界区耗时、HAL时基在各种调试/低功耗状态的推进、软硬件最终最坏时间。调用者必须定期调度Poll且保持时基运行；未证明时基/调度条件不能宣称整个Stop在墙钟期限内完成。
