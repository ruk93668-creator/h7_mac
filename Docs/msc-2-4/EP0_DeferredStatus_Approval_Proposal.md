# MSC-2～4 EP0延迟状态机制架构审批方案

日期：2026-11-17。VERIFIED=源码或可执行预检确认；INFERRED=拟议方案；UNRESOLVED=待批准/实现证明；NOT_TESTED=未测试。

## 触发条件与范围

用户明确要求：“如果确实需要修改USB Core或增加EP0延迟状态机制，必须给出具体函数、调用链和失败情形，暂停该受限修改并请求单独架构批准。”本方案遵守该约束；尚未修改任何生产文件，不将MSC-2～4连续实施授权解释为对本机制的批准。

VERIFIED：现有接口不能直接表达“Class已接收零长度请求，但状态阶段需稍后由任务完成”。这不等于已经证明必须修改ST Core；候选最小方案只修改CM7/Platform/usb_device.c/.h，ST Core保持原样。选择的是在Reset成功状态提交前完成旧事务失效、USB取消与worker安全排空的保守契约。

## 真实调用链与限制

VERIFIED：OTG_FS_IRQHandler → HAL_PCD_IRQHandler → HAL_PCD_SetupStageCallback → Platform Usb_Setup → USBD_LL_SetupStage → USBD_StdItfReq → MSC Setup。

- `Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c:207`：wLength=0且Class返回USBD_OK，立即调用USBD_CtlSendStatus。它不读取worker状态，也不等待SD操作。
- `CM7/Platform/usb_device.c:1233`：SetupStage返回非OK，直接USBD_CtlError；返回BUSY无法自然变成“保持NAK等待任务”。
- `CM7/Platform/usb_device.h`没有控制事务延期/令牌/任务续接状态接口；直接调用Core的CtlSendStatus绕过该生命周期管理不能证明新SETUP、bus reset与旧完成隔离。
- `CM7/Platform/sd_block.c:214`：块操作要求IPSR=0、PRIMASK=0、BASEPRI=0和调度器RUNNING，并校验当前Task持有者；USB ISR不能调用同步SD_Read/Write/Recover完成排空。
- `CM7/BSP/bsp_sdmmc.c:1008`：实际读写等待HAL完成、IDMA/命令数据路径quiescent、DCOUNT=0和卡TRANSFER，USB取消不代表这些条件已经满足。

INFERRED 失败情形：在WRITE数据已经交给存储任务且IDMA仍活动时收到BOT Reset。若立即返回OK，当前Core先提交EP0状态；Class若同时清busy/重用buffer，可能与旧DMA冲突。若改为返回BUSY，当前Platform会STALL，无法执行拟议的异步Reset握手。若在Setup中阻塞等待，又违反ISR上下文和块API前置条件。

注意：用持久独立buffer隔离旧worker、允许Reset逻辑切换后再后台drain，可能构成另一种设计，但仍需完整证明ready-for-next-CBW、介质错误、写入可见性和资源背压；本次未实现或验证，不能据此声称现有架构已经安全。本方案不宣称只有修改Core才能解决。

## 可执行复现

VERIFIED：Tests/MscResetPreflight/reset_preflight.c通过现有MSC测试HAL桩，执行当前真实Core/Platform/BSP；模拟worker_active只是一项受控测试状态，不是实际RTOS worker或DMA。4场景、32断言复现成功：

|场景|Class返回|模拟worker|观察|
|---|---|---|---|
|0|OK|活动|EP0 STATUS_IN，IN busy，HAL已提交零长度状态包|
|1|BUSY|活动|EP0硬件STALL，未提交状态包；不是异步NAK等待|
|2|FAIL|活动|EP0硬件STALL，未提交状态包|
|3|OK|空闲|正常提交状态包作为对照|

Core的CtlError没有统一将ep0_state改为STALL，因此场景1/2的state仍可能是SETUP；测试按真实端点stalled/busy判定，不把state数值当作硬件唯一事实。PASS表示限制被复现，不是BOT Reset验收。无板级Reset或SD测试。

## 待批准的最小修改（INFERRED）

拟新增Platform接口（名称最终按工程规范整理）：

1. `USB_Device_DeferControlStatus(request, token)`：仅在当前Class Setup调用窗口登记一次延期事务；保存当前请求快照和控制token，不在ISR中等待。
2. Class返回USBD_BUSY；Core因非OK不自动发状态。Usb_Setup仅对“确已登记、请求/token匹配”的BUSY跳过CtlError，其他原有错误与UVC/TEST处理完全保持原语义。不能将全部BUSY当作成功。
3. `USB_Device_CompleteControlStatus(token)`：Task在旧USB传输确认取消、旧worker安全排空后提交EP0状态；在USB IRQ保护下校验当前请求、设备配置状态和token。提交失败不伪造完成。
4. 新SETUP、bus reset、disconnect、Stop、DeInit使控制token失效；迟到worker完成只能释放自己实际持有的资源，不能给新控制事务发送状态。token禁止无保护回绕。
5. 单EP0静态槽、无动态内存；控制事务token与USB transport epoch、block request token、media generation分别建模。

服务层MSC Setup仍负责校验BOT Reset请求；Platform不得硬编码MSC接口号、opcode、SCSI或SD知识。BOT Reset不清Bulk halt/toggle；随后Clear HALT和CBW重arm由BOT状态机协调。Reset超时保持受控失败/介质锁定，不提前归还buffer或FatFS所有权。

修改范围：Platform两文件及对应测试。后续MSC Class/BOT/worker接入属于已授权阶段；不修改ST Core、BSP、UVC、Camera、Media、CM4、MPU、Linker或FreeRTOS。

## 机制验收门槛（NOT_TESTED）

延迟期间无状态提交/无STALL、worker排空后一次提交；新SETUP抢占；bus reset/disconnect/Stop使token失效；迟到完成；双完成；端点取消失败；状态提交失败；token耗尽；保持Bulk halt/toggle；旧DMA仍活动时不释放buffer；TEST/UVC原行为基线对照；两种CM7完整编译。此后才能继续真实异步BOT/SD Reset安全验收。

## 批准问题

是否批准上述仅限Platform的带token延迟控制状态机制（USB Device Core源码不变），以继续MSC-2～MSC-4的集成实现？这是本次用户P0条款要求的单独架构批准，不是重新请求已授权的阶段开发权限。
