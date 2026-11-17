# MSC-2～MSC-4 联合验证记录

当前状态：**软件实施与验证完成；实板验收 NOT_TESTED，MSC-5 未启动。**

以下前置记录保留为历史，最终结果见文末。

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


## 本次审批后的联合交付（2026-11-17）

VERIFIED：以远端14f44ca为起点，EP0机制47256e0、MSC-2软件1478eac、MSC-3软件deedde5均已独立提交并推送；最终MSC-4与联合验证继续独立提交。未改写历史或合并main。

| 软件证据 | 最终结果 | 实板含义 |
|---|---|---|
| EP0 Token / 取消 / 提交与完成 | 17场景通过 | Mock HAL，非硬件确认 |
| BOT / SCSI / RAM / 描述符 | 41场景通过 | 包含13-case、单块及16块分批、非法CBW、CSW失败、Clear HALT不能绕过Reset |
| 异步Block邮箱 / Worker | 18场景通过 | 三身份、队列满、活动IO取消、部分失败 |
| SD_Block设备接线 Mock | 24只读+24写分支场景通过 | 没有真实写卡 |
| 真实FatFS / RAW代码门禁 | 9断言通过 | RTOS环境模拟，不等同于实板 |
| BOT + Worker执行中Reset联测 | 5场景通过 | 真实Core/Platform/MSC，设备为Mock |
| CM7 RAM MSC / UVC / SD只读完整构建 | build_results.json | 0 Error / 0 Warning才通过 |
| 受控模式编译门禁 | 7种宏组合符合预期（含3种拒绝） | 仅编译，不操作硬件 |
| 原TEST Class普通请求/STALL/取消 | 当前及14f44ca基线各21断言通过 | 真实TEST Class，Mock HAL |
| 历史USB/UVC对照 | 10套断言结果与审批前一致 | 原有失败保留，无实板新证据 |

VERIFIED：当前新增专项共131场景、1211个C断言通过，另执行原TEST Class的审批基线21断言对照（ISR寄存器专项检查另计）。

VERIFIED：最终协议测试补充了非法CBW后Clear HALT仍保持故障锁定、短OUT的phase-error CSW、CSW提交失败、LUN/flags/CDB长度非法，以及真实Core读取32B配置描述符与GET_MAX_LUN。旧MSC-1专用fixture仍按枚举阶段无BOT接口设计，未篡改其历史结果；当前协议集成测试承担新增行为验证。

VERIFIED：Test结果文件记录测试父提交和源文件SHA256；HAL/SD/同步原语Mock的边界分别在阶段报告标注。测试脚本未放宽历史断言。final_regression_results.json中历史失败仍会使对应脚本exit=1；legacy_comparison.json逐套比较PASS/FAIL行，均一致，不能称全绿UVC回归。

### 内存与执行上下文

VERIFIED：BOT静态上下文1344B（含1024B数据、64B CBW、16B CSW及协议状态）；RAM测试介质8192B。请求邮箱单槽、完成单槽，不分配堆、不新增Task。USB/Storage既有任务各2048 words栈，未改FreeRTOS配置。具体地址和区域用Tests/MscJoint/build_evidence.json记录。

VERIFIED：USB ISR仅路由Class事件及取消意图；UsbTask推进BOT；StorageTask执行SD_Block。USB PIO与SDMMC IDMA分离，IDMA仍只使用32KiB非缓存AXI专用区。所有失败和取消保留外部IO占用，直到真实quiescent；USB控制Token失效不能释放DMA。

### 变更范围与用户修改

VERIFIED：生产变更限于授权MSC Service、新测试配置头、Platform USB延迟状态及诊断/本地FS门禁、UsbTask/StorageTask接线，Keil仅追加CM7源文件。ST Core、BSP、UVC、Camera、Media、CM4、MPU、scatter和FreeRTOS配置未修改。未访问或修改另外两个工程目录。

VERIFIED：Keil仅按字节追加MSC文件；从工作树移除本轮追加项后，其SHA256与开工用户快照一致；uvoptx哈希也一致。索引通过基线+追加项构造，用户原有工程修改未暂存/提交。具体记录在git_scope_results.json。

### 实板操作与剩余风险

NOT_TESTED：本轮未下载、未读取真实SD、未执行Windows真实BOT或写入测试。生成文件位于output/MSC24/build/：h7_mac_CM7_msc.axf为RAM测试、h7_mac_CM7_sd_readonly.axf为受控SD只读、h7_mac_CM7_uvc.axf为UVC回归。各构建使用临时CM7工程且恢复RTE，保留原CM4固件。实板阶段必须核对目标固件SHA256和原板版本。

NOT_TESTED：先RAM协议抓包与读回，再使用专用卡进行只读容量/单块/多块/32KiB边界/尾块。真实写测试需用户明确确认测试卡可销毁；目前没有该确认，不执行真实写入，不自动格式化。SD受控模式永不自动回退MCU FatFS；USB断开也不证明PC数据完整。

UNRESOLVED：真实OUT晚到IRQ、任务竞争、V1板SD卡检测极性、写中拔线/拔卡和超时恢复尚无证据。既有UVC实板SCCB错误4仍待独立硬件处理，未在本分支修改BSP。发现必须修改BSP/ST Core时仍需另行审批。

### 回滚与停止边界

VERIFIED：默认编译仍选择原UVC，SD受控模式默认关闭；需要回退时在隔离检出中构建审批基线，或人工审核后逐项revert本轮独立提交，不reset/clean或丢弃用户修改。不将未知状态测试卡交给普通MCU文件系统。

本轮停在MSC-4软件集成与待实板验收状态；不实施MSC-5动态FAT所有权切换，不进入Composite。保留全部MSC-0/MSC-1文档与历史，等待人工审核。
