# P1-C — Shared RX FIFO Retirement Contract

## 共享消费者与当前实现

VERIFIED：HAL `stm32h7xx_hal_pcd.c:1084` 是当前Device RX status的破坏性消费者；LL `stm32h7xx_ll_usb.c:1010–1040` 从DFIFO(0)读payload并写内存。HAL按当前OUT_ep[EPNUM].xfer_buff写数据，不携带Session/Transfer Generation，也不在copy前询问BSP fence。

VERIFIED：屏蔽EP2 DAINT只影响OUT完成摘要路由，RXFLVL仍可能写EP2旧Buffer。C0测试演示EPENA清零仍会复制队列数据，以及EP1/EP2按共享队列顺序各消费一次。它们证明软件分支行为，未证明硅片任意事件组合都能发生。

INFERRED实施契约：不得出现BSP先pop、HAL随后再pop同一事件的双消费者。允许的观测必须与HAL那一次pop绑定：保存不可变status、消费序号、Controller/Session/Endpoint/Transfer身份、目标Buffer起址/容量、PIO引用以及copy前后剩余容量。EP0 SETUP必须写Setup专用缓冲区，不能作为可丢弃的数据状态。

## Commit需要的独立证明

| 必须事实 | 所需证据 | 当前闭环状态 |
| --- | --- | --- |
| 新Receive被原子fence | 同Task/IRQ可见性、旧身份仍可查询，拒绝重入提交 | UNRESOLVED，未增加C1生产API |
| 目标不再接收旧数据 | 本Global NAK lease有效；目标EPDISD，EPENA/EPDIS清零 | 手册VERIFIED，软件唯一owner UNRESOLVED |
| HAL不会再写旧Buffer | 每次copy有原Transfer强引用；引用归还后再无旧copy可进入 | UNRESOLVED，当前无copy前后hook |
| 无会访问旧Buffer的队列状态 | 本lease的NAK marker和旧payload消费journal连续，目标Disable后状态全部按原身份退休 | UNRESOLVED，不能用一次RXFLVL=0或peek当完整证据 |
| 旧XFRC/EPDISD不能污染新Transfer | W1C前留存原身份，旧事件隔离，原身份Commit后才能publish新Arm | UNRESOLVED，当前普通回调仅endpoint |
| 全局NAK已合法解除/仍合法持有 | 全部lease参与者决定，SGONAK/CGONAK串行记录，mask按位恢复 | UNRESOLVED，HAL现有写入绕过owner |

INFERRED：先前有效Global NAK之后仍有EP0 SETUP可能入队，所以不要求“永远全FIFO空”来伪造完成；应证明目标旧数据在本屏障及Disable之后不可能再访问旧Buffer，同时继续服务其他接收者。单端点Stop绝不直接RXFFLSH。

## 身份与迟到事件

INFERRED：OutTransferIdentity=(controller_generation,session_generation,endpoint_address,direction,endpoint_generation,transfer_sequence)，StopAttempt独立单调不回绕。PIO ledger、RX status journal、端点终止事件分别引用旧身份；Session失效不代表旧内存引用归还。

INFERRED：正常XFRC与Stop竞争时，只能有一个最终收尾者；旧回调不得在新Receive发布后按新指针解释。BSP/Platform需在进入Core/DataOut前区分普通完成与停止收尾，但这不能替代HAL copy前的保护。原HAL目前先W1C XFRC再回调，回调可重Arm，随后仍处理缓存epint中的EPDISD；C0软件探针直接覆盖该顺序。

INFERRED：Reset/Disconnect进入受控失败并保留旧RX引用。不得清HAL xfer_buff、强清busy、清owner或重新初始化PCD来掩盖缺失证明。USB端点证明与SDMMC IDMA证明独立，RAW/FatFS所有权不因USB状态变化而转移。

NOT_TESTED：真正C1的Buffer提交fence、最终Commit、故障后同身份恢复、实板迟到IRQ均未执行。模型中注入旧raw flag是为了暴露HAL没有Generation筛选，不能宣称实际硬件必然产生重复事件。
