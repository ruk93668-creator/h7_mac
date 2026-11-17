# MSC-2～MSC-4 联合开发前置验证记录

日期：2026-11-17。状态定义：VERIFIED源码/预检确认；INFERRED设计；UNRESOLVED待决定；NOT_TESTED未实测。

## Git与范围

VERIFIED：分支codex/usb-msc-bringup，本轮开始HEAD 4be1f6dc34a3e3adc94e1e2f2608b75ecf5c597f，包含MSC-1有效抓包证据；初始远端为1b332bc。已优先补推4be1f6d成功，未reset/rebase/强推或修改main。

VERIFIED：开始时MDK-ARM/h7_mac.uvprojx和h7_mac.uvoptx有用户未提交修改。原样保留，不纳入本轮提交；新增测试无需改Keil。MSC-0七份文档与已提交版本无差异，继续作为设计依据。

## 完成的安全工作

- VERIFIED：核实Core/Platform的EP0返回值契约，新增Tests/MscResetPreflight复现，4场景32断言通过。结果只证明接口限制存在，不代表BOT实现通过。
- VERIFIED：重新核实SD_Block租约为Task身份+FATFS/RAW，Release拒绝active/fault；不能在ISR使用阻塞SD接口。
- VERIFIED：SDMMC当前仍为0x24000000、32KiB、32字节对齐专用AXI区、512字节块/每批最多64块；MPU region3为non-cacheable/non-bufferable/shareable，scatter脚本保留专用段。未修改这些配置。
- VERIFIED：SD_Block_Sync是同步Write契约后的状态复核，实际Write等待卡TRANSFER，不是新的主机cache flush；不能将USB Reset当成同步完成。
- VERIFIED：StorageTask启动后会App_StoragePrepare并持续App_StorageProcess；仅添加另一个RAW worker并不足以构成受控模式。MSC-4必须在启动路径强制禁用本地FatFS准备/挂载和后台文件请求，同时由RAW持有者独占，不能依赖口头不操作。
- INFERRED：后续worker优先由受控StorageTask分支执行，FATFS与RAW模式编译/启动门禁互斥，所有StorageFS入口仍受租约检查；USB断开/reset/fault不切回FatFS。专用卡写测试仍需人工明确可销毁授权。

## 阶段状态与阻塞

|阶段|状态|实际完成范围|
|---|---|---|
|MSC-2|UNRESOLVED：EP0机制审批门槛|只有真实栈接口预检；未实现BOT、SCSI或RAM LUN|
|MSC-3|未实施|未新增adapter/queue/worker；依赖Reset契约批准|
|MSC-4|未实施，SD写入禁止|只核对原SD接口/内存/租约；未访问SD卡|

暂停原因来自用户明确P0条款：增加EP0延迟状态机制必须单独批准。最小可审核方案见EP0_DeferredStatus_Approval_Proposal.md，拟保持ST Core不变，仅扩展Platform两文件。不能把本记录当作三个阶段完成提交。

## 软件与实板证据

VERIFIED：ARMCC5编译当前真实USB Core、Platform、BSP和MSC描述符，Unicorn执行4场景32断言；HAL为桩，worker只是活动状态哨兵。没有新增生产实现，未运行生产CM7重编，不把预检编译当成CM7完整构建。已有UVC历史回归失败记录原样保留。

NOT_TESTED：BOT/SCSI/13-case/RAM读写、异步队列/Mock块设备、DMA取消/迟到完成、真实SD读写、Windows RAM LUN实测、UVC实板回归均未在本轮执行。

VERIFIED：用户确认硬件为V1，当前基线摄像头BSP仍V0；SCCB写地址0x78无ACK（s_sccb_error=4）的前轮实板证据解释UVC回归阻碍。Camera/V1移植不在本次授权范围，不能为通过回归而修改。

## 交付与下一步

本轮仅提交预检测试/结果、EP0审批方案和本记录；保留用户工程修改，不更改生产固件、不烧录、不访问其他工程。批准后连续实施MSC-2、MSC-3、MSC-4并逐阶段提交，最后统一汇总；不进入MSC-5。当前请求批准的是具体EP0机制，不重新请求阶段开发授权。
