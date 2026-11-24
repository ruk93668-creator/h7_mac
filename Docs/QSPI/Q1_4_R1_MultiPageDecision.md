# Q1.4-R1 跨页 Program 待审技术说明

R1-B01 在 520 字节跨页请求中从页末开始。第一个 1 字节片段完成编程及逐字节验读，第二次 WREN 的 HAL 命令成功，随后有效 SR1 返回 BUSY=0、WEL=0。Mock 观测到两次 WREN、一次 0x02，目标只有第一个字节改变。

`Qspi_WriteEnable()` 在有效 WEL=0 时清除 `armed` 并返回 `SYS_ERR_PROTOCOL`。上一页通过 `Qspi_WriteWait()` 后已经清除 `pending`，而 `Qspi_WriteFinish()` 的锁存条件使用当前风险标志，诊断中的 `mutation_possible` 则保留本次调用历史。实测结果为 `completed=1`、`mutation_possible=1`、`fault_latched=0`，BSP 保持 READY。见 [BSP 源码](../../CM7/BSP/bsp_qspi.c#L1049)及 [R1-B01 原始日志](../../Tests/QspiFlashQ1/q14_r1/evidence/boundary/run.log)。

按本轮“本次调用尚未尝试 0x02/0x20”的条件，第一页的 0x02 已使该请求不符合 A 类例外，R1-B01 应锁存 FAULT。该断言实际失败，按原结果归档。生产整改待 GPT 独立审核。

- 若确认条件作用于整个公共调用，后续可仅对该分支使用已记录的修改历史锁存 FAULT，保留已确认前缀，并继续区分完成已知和完成未知。修改需另行授权。
- 若确认条件仅作用于当前页片段，需由 GPT 明确批准已有行为：返回部分完成和协议错误，保留历史修改及完成前缀，BSP 保持 READY。
