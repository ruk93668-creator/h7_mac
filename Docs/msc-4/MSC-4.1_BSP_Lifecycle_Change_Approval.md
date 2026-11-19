# MSC-4.1 BSP 生命周期扩展审批方案（待批准）

> 范围更新（2026-11-19）：用户曾批准本文的有限BSP扩展；之后最新任务重新明确禁止修改BSP，要求采用带取消完成证明的停止状态机。此前未提交的BSP诊断已归档并移出工作区。**本文保留为历史申请；当前待审对象是 `MSC-4.1_StopLifecycle_Audit_and_Approval.md`，不能继续依据旧批准修改BSP。** 下文“未批准”等字样为当时申请状态，不代表中间没有取得过批准。

本方案未批准、未实施。当前分支 `codex/usb-msc-bringup`，原任务基线8691497；诊断准备提交faf9265。任务仍停留MSC-4.1，不进入MSC-5。

## 申请原因与已确认事实

原任务允许修改Platform USB与MSC Class生命周期，要求：“如果需要超出授权范围，停止并提交审批方案。”本方案申请新增 `CM7/BSP/bsp_usb.c/.h` 的有限权限；不通过Platform直接操作寄存器来绕开BSP。

相同V1板、电脑/USB端口/线缆，SD已断电取出。RAM MSC首次枚举成功；不复位断开重连失败，Windows报告Device Descriptor Request Failed，原始抓包与两阶段SRAM/寄存器已保存。

- VERIFIED：首次地址硬件/PCD/Core均21、config1；重连硬件地址0，说明硬件DAD复位生效。软件地址仍21并不能证明硬件发错地址。
- VERIFIED：故障时OUT2仍opened/busy、BSP rx_active[2]=1，HAL OUT2 maxpacket64；DOEPCTL2=`0xC00A0040`，EPENA/EPDIS/NAKSTS均为1。
- VERIFIED：Class RELEASE_FAILED、stop_required=1、last_result=-9，NVIC USB IRQ101处于禁用状态。
- VERIFIED：`bsp_usb.c:337 BSP_USB_AbortEp -> HAL_PCD_EP_Abort -> stm32h7xx_ll_usb.c:915 USB_EPStopXfer`；OUT停止等待EPENA清零，10000次后可返回HAL_ERROR。`BSP_USB_Stop:188`先禁用IRQ，Abort失败直接返回，不到HAL_PCD_Stop及资源归还阶段。现场符合此失败路径。
- UNRESOLVED：首次取消失败是总线Reset/挂起时序、OUT停用握手还是其他控制器条件所致。当前环形日志被重复STOP覆盖，不能把推断写成完整根因。

这已证明持续不响应的底层阻塞链，尚不足以声称最终根因已完全定位。只在Platform清状态、重开端点或强制Start会掩盖未完成取消，违反本次安全约束。

## 请求的具体新增范围

| 文件/接口 | 拟议工作 | 边界 |
| --- | --- | --- |
| `CM7/BSP/bsp_usb.c` | 首次取消失败的固定内存诊断；记录Reset/Connect/Disconnect及Abort/Close/Stop的入口、结果和端点寄存器 | 不使用停机探针、不读RX弹出/FIFO、不无限写日志 |
| `CM7/BSP/bsp_usb.h` | 只读生命周期诊断结构/查询接口（如 `BSP_USB_GetLifecycleDiagnostics`） | 不暴露PCD内部可写指针，不引入SD依赖 |
| `BSP_USB_AbortEp / CloseEp / Stop` 内部 | 在确定首次失败原因后，最小修正OUT停用/完成确认与失败状态传播 | 外部签名/成功契约保持；未证明硬件静止不得归还缓冲 |
| 已授权Platform/MSC生命周期 | 配合明确的底层结果管理状态，并保存首个失败记录 | 不通过手工清状态伪造底层成功 |
| `Tests/MscReconnect/`及本报告 | 新增寄存器模型测试、对照抓包与实板回归 | 所有旧断言/失败保留 |

ST Device Core、ST HAL/LL源码、UVC数据路径、SDMMC/FatFS、FreeRTOS、MPU/scatter、CM4、Keil工程均不在申请范围。

## 实施步骤及不可放宽的条件

1. 先补首次故障快照，区分Reset回调前后、端点取消实际状态及NVIC禁用发生点；相同错误重复出现只计数，保留首个因果链，不让STOP重试覆盖现场。增加诊断关闭/开启对照，避免诊断时序误判。
2. 完成A默认UVC、B RAM、C SD只读三组对照；捕获均在连接前开始。A/B取出SD，C显式WRITE_ENABLE=0。无法捕获的SET_ADDRESS等事务标NOT_TESTED，不能用注入描述符代替总线事务。
3. 对照本芯片参考手册及当前HAL实际行为，核对OUT NAK/停用握手、EPENA/EPDIS确认和错误条件。**不预先假定清某一位即可恢复。** 如果修改BSP仍不足以安全实现，继续停止并单独申请，禁止改ST驱动。
4. 只有实际确认对应端点停止，才清对应旧完成事件和PIO占用；共享RX只能在所有相关接收均安全停止时处理。Stop失败保留fault、缓冲与所有权，不能为了重新枚举而盲目重新开IRQ或返回成功。
5. 不凭USB Reset或Token失效推断SDMMC IDMA停止。SD Worker/在途DMA排空保持原独立契约，RAW租约始终保留，不转交FatFS。
6. 旧Token不可复活；新SETUP不得接收旧完成；EP0新状态必须对应新事务。停用/重建必须有明确硬件边界，不使用全局memset、强制重启USB或自动MCU复位。

## 需要补齐的验收与回滚

- 软件：当前全部Deferred Status断言、EP0旧完成/取消/失败、寄存器级OUT停用成功/超时/迟到IRQ模型，及默认UVC历史对照；不改旧断言。
- 实板：三组冷启动/不复位重连、修复后10次连续重连、USB Reset、BOT Reset+ClearHALT、正常READ、在途READ拔线、真实SD重新读取及独立DMA排空证据。保留所有失败的原始捕获与快照。
- 在不能证明停用成功时验收必须失败，不降低保护。A/C及上述回归目前未完成，当前提交仅作取证备份。
- 回滚仅撤回本轮批准后新增的BSP/Platform/MSC修复提交；不重置分支，不覆盖用户Keil修改，不动SD卡内容。诊断固件不作为已验收发布固件。

**待人工决定：是否批准上述BSP USB有限扩展，以便继续定位并完成安全修复。** 本文不构成批准，也不代表恢复已实现。
