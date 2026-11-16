> 基线 `f0ce45fd9ab7250b1e22260e087d606ee5ed9ecb`；2026-11-16。
> VERIFIED：源码确认；INFERRED：源码推断/拟议设计；UNRESOLVED：待确认；NOT_TESTED：未实测。以下新架构均未实现。

# MSC 协议与分层设计

## 层次与依赖（INFERRED）

Windows→MSC Class→BOT→SCSI→LUN→Block Adapter→SD_Block→BSP_SDMMC→HAL/SDMMC IDMA→SD。FatFS不在PC块数据链路。

|拟议文件|职责/接口|依赖|
|---|---|---|
|`CM7/Service/usb_msc_desc.c/.h`|静态设备/配置/字符串描述符|USBD类型，不引用SD/FatFS|
|`CM7/Service/usb_msc_class.c/.h`|Register、Init/DeInit、Setup、DataIn/Out；端点和控制事务|USB_Device_*与BOT；不调用阻塞SD|
|`CM7/Service/usb_msc_bot.c/.h`|CBW有效性、阶段/长度/stall/CSW、reset epoch|SCSI及抽象异步IO；不解析FAT|
|`CM7/Service/usb_msc_scsi.c/.h`|Decode/Execute，CDB校验、sense、几何/读写计划|LUN接口，不直接HAL|
|`CM7/Platform/msc_block_adapter.c/.h`|RAW客户端，geometry/present/protected、Read/Write/Sync/Recover，结果→SCSI错误|SD_Block_*；运行于存储worker|
|`CM7/Application/msc_media_owner.c/.h`|卷授权、任务请求仲裁、quiesce与归还|App_Storage/StorageFS、MSC介质状态；不进入Core|

VERIFIED：底层现有SD_Block是同步API、有Task身份检查；USB完成来自ISR，不能直接在DataOut调用Write。拟议默认使用StorageTask作为本地FatFS或RAW模式的唯一worker，USB Task只投递请求并收取完成，避免阻塞UVC调度。传输token、USB epoch、media generation三者共同匹配，只有一个CBW执行中；队列有界，满时保持NAK而非覆盖请求。具体队列接入尚未实现。

## 描述符与 LUN（INFERRED）

独立MSC interface0/alt0，class08/subclass06/protocol50；Bulk IN 0x82、OUT0x02，FS MPS64、bInterval0。复合时使用interface2，保留UVC 0/1及81。只有LUN0，GET_MAX_LUN回0。单独MSC不需IAD；复合仅UVC功能IAD覆盖0/1。设备身份/序列号应稳定唯一、VID/PID需合法分配，当前测试VID/PID不能直接用于生产。

LUN保存physical_present、ready、owner_authorized、write_protected、ejected、prevent_removal、block_count/block_size、generation、sense和Unit Attention。存在不代表ready。ready必须owner=PC且卡已初始化无fault。GetInfo取得整卡容量，暴露绝对LBA0（含MBR），不能用FatFS分区边界当整卡；只支持当前底层可表达且验证过的32位块数/512字节介质。无法表达的容量拒绝ready，禁止截断。PC格式化成MCU不支持的格式时保留错误，不自动格式化。

插拔改变generation，取消旧IO、清ready；新卡重新识别CID和容量，发介质变化Unit Attention。写保护尚无完整物理查询API，需明确软件只读策略及卡能力，不把缺少字段当成可写证明（UNRESOLVED）。

## CBW → 数据 → CSW（INFERRED，协议依据）

按[USB-IF BOT 1.0](https://www.usb.org/sites/default/files/usbmassbulk_10.pdf) §3、5、6设计；新实现必须独立逐条对照规范。CBW 31字节，signature 0x43425355；CSW 13字节，signature 0x53425355，回显tag。BOT字段小端，CDB多字节字段大端；显式按字节编解码，不依赖packed结构体强转。

```
DISABLED → WAIT_CBW → VALIDATE → EXECUTE
                              ↘ DATA_IN / DATA_OUT / NO_DATA
                                  → WAIT_STORAGE → SEND_CSW → WAIT_CSW_COMPLETE → WAIT_CBW
错误 → STALLED / RESET_REQUIRED → RESET_PENDING → WAIT_CLEAR_IN → WAIT_CLEAR_OUT → WAIT_CBW
```

CBW接收容量64（当前BSP约束），实际长度31才解析；长度/signature/接收时序无效为invalid CBW；保留位、LUN、CDB length/字段另做meaningful和命令合法性检查，不混成“未知opcode成功”。invalid选择双Bulk stall并持续至Reset Recovery，不发伪CSW。meaningless CBW规范未定义统一响应，本项目保守选择锁住并要求reset，需互操作测试。

Data OUT只在完成事件后交worker，按完整512字节块写；实际长度与计划不符中止，不把残留字节当下一CBW。Data IN等待读完成后提交，直到最后包完成保留源。Write成功需底层同步返回，不以USB ACK表示SD已写完。CSW在命令及数据处理结束后发送；其完成前不重arm下一CBW。residue按实际相关/处理字节计，绝不下溢，失败可能已经部分写入。

### 13-case 测试矩阵

H/D为主机期望/设备意图；n无数据、i读、o写。下表为实施选择，正常状态G=按命令返回0或1；P=2，须Reset Recovery。短于H的IN在发送预期数据后stall，必要时短包/ZLP；清halt后才发CSW。

|case|H/D|数据/终止策略|CSW|
|---|---|---|---|
|1|Hn/Dn|无|G，residue0|
|2|Hn/Di|无|P|
|3|Hn/Do|无|P|
|4|Hi/Dn|stall IN|G，residue H|
|5|Hi>Di|发送D，stall IN|G，H-D|
|6|Hi=Di|发送H|G，0（完整成功）|
|7|Hi<Di|仅发送H，不越界|P|
|8|Hi/Do|stall IN，不写|P|
|9|Ho/Dn|stall OUT|G，H|
|10|Ho/Di|stall OUT，不写|P|
|11|Ho>Do|接收处理D后stall OUT|G，H-实际处理量|
|12|Ho=Do|接收处理H|G，0（完整成功）|
|13|Ho<Do|stall OUT，拒绝残缺写命令|P|

NOT_TESTED：上述分支及中途介质错误、CSW提交失败/重试、精确MPS/ZLP均须协议抓包与注入验证，不把本表当已验证代码。命令自身失败为sense+failed CSW，传输阶段冲突为phase error；内部无法恢复的错误锁住双Bulk，保护数据。

## Reset Recovery 与当前 Core 限制

VERIFIED：`USBD_StdItfReq`在零长度Setup返回OK后立即发状态；`USB_Device`新SETUP会取消旧EP0。IN错误只更新endpoint result并唤醒Task，Class没有专用DataInError回调，MSC需查询端点结果，不能无限等成功回调。

INFERRED：BOT Reset校验bmRequest=0x21、bRequest=0xff、wValue=0、wIndex=MSC接口、wLength=0；GET_MAX_LUN为0xa1/0xfe、wValue0、wLength1并校验接口。Reset先阻止新提交、增加epoch、取消USB；worker排空/停止旧SD请求，确认旧缓冲不再被访问，再完成控制状态阶段。Reset保持Bulk halt与toggle；主机随后依次clear IN、OUT，成功记录两者才重新接收CBW。不能用close/open无条件重置toggle冒充BOT Reset。

UNRESOLVED（进入MSC-2前解决）：需要在Platform `Usb_Setup`截获MSC Reset并提供延后EP0状态与新SETUP取消token，或经单独审查修改Core支持deferred status。只返回BUSY/OK均不能实现完整异步握手。普通不含阻塞IO的Reset可在Class快速完成；在途SD情形必须有可靠协议完成定义。EP0状态完成也需明确事件，不可复用data IN sequence（EP0不计序号）。

总线Reset/断线/DeInit来自ISR，禁止释放worker仍持有的buffer；Class协议上下文可逻辑失效，worker请求存储必须独立持久，不依赖1024字节Class块生命周期。USB reset不释放PC卷所有权。Clear halt被Core先ACK，再调用Class，Class仅更新恢复状态，不重复发送状态包。

## SCSI 策略（INFERRED）

以下为拟支持子集；以CDB实际长度、保留位、allocation length、方向/长度进行校验。非法字段返回ILLEGAL REQUEST，绝不统一成功。

|命令/opcode|最低计划|具体语义|
|---|---|---|
|INQUIRY 12h|必须|标准36字节；direct-access、removable；allocation length截断；不支持VPD页合法拒绝|
|TEST UNIT READY 00h|必须|无数据；ready/无介质/UA，不触发自动抢回所有权|
|REQUEST SENSE 03h|必须|固定格式18字节，按allocation length截断；独立每LUN sense，完成消费策略需测试|
|READ CAPACITY(10) 25h|必须|8字节，最后LBA=block_count-1，block_size512；零容量不做减法；超范围策略如上|
|READ(10) 28h|必须|LBA/count减法范围检查，count0无数据；分批读；拒不支持字段|
|WRITE(10) 2Ah|必须|写保护/owner/generation检查；完整块、同步等待；未实现FUA等字段合法拒绝|
|MODE SENSE(6) 1Ah|基础必须|报告WP；支持明确的缓存页与all-pages查询，WCE=0；不支持page/control返回24/00，不谎报写缓存能力|
|START STOP UNIT 1Bh|必须|支持power condition0、IMMED0；Start1/load需owner授权；LoEj1 Start0触发归还流程，PREVENT时失败|
|PREVENT ALLOW 1Eh|必须|支持prevent0/1，记录逻辑阻止移除；非法组合拒绝；物理拔卡仍故障|
|SYNCHRONIZE CACHE(10) 35h|必须|IMMED0；范围校验并同步所有已接受写入；完成再CSW；不能据此直接归还卷|
|其余|可合法失败|ILLEGAL REQUEST / INVALID COMMAND OPERATION CODE；按Windows抓包补充，不伪成功|

sense候选映射：NOT READY/3A00无介质、0401准备中；UNIT ATTENTION/2800介质变化、2900复位；DATA PROTECT/2700；ILLEGAL REQUEST/2000未知opcode、2400非法字段、2100越界；MEDIUM ERROR/1100读错、0C00写错；不可分类硬件故障HARDWARE ERROR/4400。每个错误还保留底层SysResult/部分完成诊断，最终ASC/ASCQ与UA优先级需实施前规范审查（UNRESOLVED）。

SCSI依据：[T10 SBC-3提案中的START STOP/SYNCHRONIZE CACHE定义](https://www.t10.org/ftp/t10/document.06/06-034r5.pdf)。该资料不是本项目完整SPC/SBC符合性认证；Windows实际命令序列、扩展页、MODE SENSE(10)/READ FORMAT CAPACITIES等兼容需求为NOT_TESTED。
