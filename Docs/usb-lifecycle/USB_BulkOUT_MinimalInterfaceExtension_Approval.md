# P1-C C0 — 最小接口扩展申请（未实施）

## 审批原因

VERIFIED：当前授权禁止修改ST HAL/LL/Core；任务同时明确，如果HAL自有OUT EPDISD/Global NAK处理不能形成唯一所有权，必须停止并申请扩展。本轮已命中此门禁；没有修改禁止文件。

建议扩展仅限 `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c` 及配套声明头文件，加对应BSP适配与测试。ST Core、LL、MSC/UVC、USB Mode/Application/Task、SDMMC/FatFS、Keil/MPU/Linker/CM4仍不变。此为INFERRED最小集候选，批准本方案之前不能编写生产实现，也不能称接口设计已通过测试。

## 候选接口及插入点

| Hook / 决策点（暂定名） | 必须时序与信息 | 不能做的事 |
| --- | --- | --- |
| RxStatusObserved / RxCopyBegin-End | HAL :1084真实一次pop之后，原RegVal不可变；:1092前后登记原Transfer、Buffer范围、PIO强引用及精确copy字节；SETUP仍走原专用路径 | 不能额外pop、在HAL已写旧内存后才声称已保护；不得靠动态分配或丢弃其他端点payload |
| OutTerminalBeforeClear / Retired | HAL :1127 XFRC、:1164 EPDISD的W1C前保存身份及事件快照；原终止事件仍由唯一收尾者清除；取消不伪装普通DataOut成功 | 不能拦掉EP0 SETUP或只凭endpoint号匹配新Transfer |
| GlobalOutNakRequest / Release / Effective | HAL ISO :1460–1464请求、:1413–1425生效及:1146–1148解除必须通过BSP统一lease决策；保留ISO参与者功能 | 不能让某个EPDISD无条件解除他人lease；不能把BUSY当取消已提交 |
| SessionBoundaryObserved | 在HAL任何可能访问旧RX Buffer之前发布已观测Reset边界；保存旧引用，后续正常硬件Reset/ENUM流程不丢 | 不能把epoch失效当DMA/PIO完成，不能强制重启USB掩盖引用 |

INFERRED：默认未绑定新契约时保持原HAL行为；绑定生命周期管理后，所有相关路径必须成套启用，禁止只挂copy通知而继续让HAL无条件CGONAK。Hook有界、无等待、无阻塞、无Class业务知识；BSP维持唯一寄存器适配层，Platform不直接操作寄存器。Task仍沿用当前USB Task。

## 授权后C1准入标准

1. INFERRED准入要求：单次真实pop/copy及全部NAK写入可追踪，能够证明原Transfer引用退休，而非模型设定其已退休。
2. INFERRED验收计划：覆盖用户全部C1专项，真实HAL与边界注入两层测试；原76项、17入口及三配置全部重跑并保留历史失败。
3. NOT_TESTED硬件：软件通过后仍需单独批准实板；本申请不包含烧录、MSC BOT迁移或SD写入。

UNRESOLVED / 待人工审批：若要求HAL一行不改，则需另行审核覆盖全部RX/EP0/ISO/Reset语义的独占BSP分发方案及维护成本。本轮没有擅自选择这一更大重构。
