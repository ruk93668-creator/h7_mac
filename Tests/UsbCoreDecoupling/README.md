# USB 通用端点与生命周期验收

当前第二阶段：见 [stage2_acceptance.md](stage2_acceptance.md)。

- 192项执行测试：原132项完整保留，新增60项。
- BSP支持EP0到EP8双向；非零端点支持FS等时、Bulk和Interrupt。
- 按外部Endpoint Descriptor在注册时规划FIFO，运行中不重排。
- Class生命周期包装、失败保留资源、成功后提交池释放。
- 最新结果：results.md / results.json；命令：commands.json；正式编译：cm7_keil.log。

下面保留第一阶段解耦说明，历史能力及统计以第二阶段报告为准。

---

# USB Device Core 解耦验收与接入说明

当前范围：通用 USB Device 管理、外部描述符注册、EP0 分发、端点资源管理。没有开发 UVC/MSC Class 或运行时模式切换。

## 1. 原有耦合与处理

原 `CM7/Platform/usb_device.c` 同时包含：

- TEST VID/PID、设备描述符、固定18字节配置、字符串。
- TEST Class 操作表、接口0/Alt0处理。
- 官方Core适配、设备启动停止及BSP交互。

现已把前两项原样迁入 `CM7/Service/usb_test_device.c`。该文件是既有TEST行为的兼容迁移，不是新增UVC/MSC实现。Platform不再包含 `s_config_desc`、TEST身份、UVC/MSC头文件或具体业务端点地址。

## 2. 修改文件及必要性

| 文件 | 修改目的 |
|---|---|
| CM7/Platform/usb_device.c/.h | 外部注册、配置校验与缓存、设备生命周期、通用端点接口和状态管理 |
| CM7/Service/usb_test_device.c | 从Platform移出既有TEST实现，保留当前基础枚举 |
| CM7/BSP/bsp_usb.c/.h | 仅补IN/OUT等时未完成事件回调，不改变端点能力、FIFO或发送算法 |
| MDK-ARM/h7_mac.uvprojx | 仅在CM7 Target加入TEST迁移文件，CM4 Target不变 |
| Tests/UsbCoreDecoupling | HAL桩、ARM执行测试、命令及验收记录 |

UVC文件、Mode Manager、官方USB Core及CM4源码均未修改。用户此前裁减的framework文件和本机uvoptx设置不纳入本次提交。

## 3. 重构后的结构

```text
外部描述符表 + 外部USBD_ClassTypeDef
                │
                ▼
      USB_Device_RegisterClass()
       校验配置、复制回调表、缓存静态配置
                │
                ▼
  Platform usb_device.c + ST官方Device Core
       标准请求、EP0阶段、通用分发
                │
                ▼
          USBD_LL_* 适配
       端点opened/busy/stalled管理
                │
                ▼
           BSP USB → HAL PCD
```

默认启动顺序：

```text
BUS：BSP_USB_Init
DEVICE：USB_Device_Init（只准备Core，没有Class）
SERVICE：Usb_TestRegister + USB_Mode_Init
APPLICATION：原有UsbMode_AppStart → USB_Device_Start
```

SERVICE中两者没有先后依赖；进入APPLICATION前TEST已登记。Mode Manager源码未改，原 `h7_mac USB FS Test` 产品名、VID/PID、18字节配置和EP0基础枚举保留。

## 4. 外部描述符怎么提供

原注册入口继续保留：

```c
SysResult_t USB_Device_RegisterClass(
    USBD_ClassTypeDef *driver,
    USBD_DescriptorsTypeDef *descriptors
);
```

调用顺序为 **Init → RegisterClass → Start**。生命周期接口从主循环/Initcall调用；USB启动后或停止恢复失败时禁止替换。没有实现自动热切换。

必需字段：

- Class：Init、DeInit、Setup、GetFSConfigDescriptor。
- 设备描述符表：GetDeviceDescriptor。
- 若设备声明了厂商、产品或序列号字符串，则对应查询及语言回调必须存在；未声明的可选字符串回调允许为空。

注册时检查配置指针、wTotalLength与返回长度相等、每段bLength边界、接口编号和Alt0存在性、每个Alt实际端点数量与声明一致。设备描述符有效长度18字节、EP0包长与当前Core一致，当前只支持一个配置，配置值为1。

回调表复制到内部静态对象；配置字节不复制，由外部模块保持有效且不得在连接期间修改。Platform缓存校验后的配置指针和有效长度，并将内部Class表的GetFSConfigDescriptor指向通用缓存入口：

```c
uint8_t *USB_Device_GetConfigDescriptor(uint16_t *length);
```

没有内置或拼接任何UVC/MSC配置。外部getter后续换指针不会绕过已登记配置；修改同一缓冲区字节仍属于调用方违反生命周期约定。

## 5. EP0与请求分发

```text
HAL SETUP回调 → BSP setup → Usb_Setup
    ├─ 保留类型、无效端点等：EP0 STALL
    └─ USBD_LL_SetupStage（官方）
         ├─ 标准设备请求：官方Core处理
         ├─ Descriptor：外部设备回调/已校验的配置缓存
         ├─ 标准接口请求：按官方设计转交Class.Setup决定接口/Alt行为
         └─ Class/Vendor请求：转交外部Class.Setup
```

外部Setup返回失败时，适配层保证通过EP0 STALL拒绝。标准GET_INTERFACE、SET_INTERFACE等仍由Class提供具体接口语义，Core没有实现任何UVC备用设置或Probe/Commit。

Class可以在收到的 `USBD_HandleTypeDef *pdev` 上使用官方 `USBD_CtlSendData`、`USBD_CtlPrepareRx`、`USBD_CtlSendStatus`、`USBD_CtlError` 等控制传输接口。配置Descriptor的返回长度由官方Core再取 `min(描述符长度, wLength)`。

EP0资源由Core管理；Class不能通过新增数据端点接口关闭或申请EP0。GetEpState允许读取EP0快照。新SETUP清除上一轮EP0 busy与stalled快照。

## 6. 通用端点接口

| 接口 | 用途 |
|---|---|
| USB_Device_OpenEp | 按外部提供的地址、类型、最大包长申请端点 |
| USB_Device_CloseEp | 关闭并释放端点；重复关闭可成功返回 |
| USB_Device_FlushEp | 清空空闲端点FIFO；busy时返回BUSY，不冒充取消传输 |
| USB_Device_Transmit | 提交IN传输，未完成时拒绝覆盖原事务 |
| USB_Device_Receive | 提交OUT接收，未完成时拒绝覆盖原事务 |
| USB_Device_StallEp / ClearStallEp | 转交BSP执行STALL操作 |
| USB_Device_GetEpState | 返回opened、busy、stalled、type、max_packet、last_result快照 |
| USB_Device_GetRxSize | 查询OUT接收长度 |

资源申请成功时同步官方Core的is_used/maxpacket。打开失败不占用资源，发送启动失败不遗留busy，关闭失败不提前释放缓冲区所有权。

操作时仅保存并屏蔽USB IRQ，完成状态提交后恢复原使能值，不改变PRIMASK。允许从主循环和USB回调使用端点接口；不支持从其他抢占USB的中断中调用。生命周期接口不允许在ISR中使用。

完成流程：

```text
HAL DataIn/DataOut → BSP → Platform清busy
    → 官方USBD_LL_DataInStage/DataOutStage
    → 外部Class.DataIn/DataOut
```

清busy在Class回调之前，因此Class可在完成回调中重新提交。关闭后的迟到完成事件不会交给Class。

错误流程：

- 同步硬件错误：通过SysResult_t返回，并记录端点及全局错误。
- HAL等时未完成：BSP新增转发 → Platform清busy并记录TIMEOUT → 官方Core → 外部IsoINIncomplete/IsoOUTIncomplete。
- 当前HAL没有统一的所有端点异步错误回调，本轮没有虚构一个不存在的硬件事件入口。

第一阶段BSP只支持EP0双向CTRL64和EP1 IN ISO512；第二阶段已扩展到EP1至EP8双向，完整包长与FIFO限制见stage2_acceptance.md。

Class按描述符确定端点、包长、间隔及提交节奏。OpenEp复用HAL既有地址/类型/包长接口，不替Class实现bInterval调度，也不在Core改FIFO。

## 7. 状态及一致性修正

- 未注册Class不能Start，运行中不能替换Class。
- Start先提交允许分发状态，再开放硬件，避免第一个事件到来时软件仍未就绪。
- Stop先确认BSP停止成功，再调用外部DeInit及清理端点；失败锁存恢复要求，禁止Start/Register，允许重试Stop。
- Init/Register/Start/Stop增加生命周期重入保护，外部注册/释放回调中再次调用时返回BUSY。
- Reset无论Class释放是否失败，都尝试清理剩余端点；清除旧EP0打开快照，核对本轮双向EP0是否真正打开，失败时要求Stop恢复。
- IsConfigured依据实际设备状态及恢复要求；一次端点BUSY不会再把已配置设备误报为未配置。GetLastResult是错误诊断，不能代替连接状态。

## 8. 测试与编译

```powershell
python Tests/UsbCoreDecoupling/run_tests.py
```

ARMCC5 V5.06 update7 build960编译正式BSP、Platform、迁移后的TEST、未修改的Mode Manager及官方Core，Unicorn执行真实ARM指令。HAL以桩代替，不使用实板。

**132/132通过，1341项断言。** 包含52项基础枚举、32项Mode回归和48项通用Core检查。详细输入、预期及实际结果见results.md/results.json，实际编译链接命令见commands.json。

新增用例242～247在修复前6项失败，记录见pre_fix_results.json；修复后全部通过，覆盖EP0重建失败、Reset清理、生命周期重入、新SETUP和启动期间重入。

Keil CM7最终编译：**0 Error、0 Warning**，日志见cm7_keil.log；保护范围及CM4 Target一致性见source_audit.json。本轮不声称实板USB枚举或UVC视频通过。

旧测试迁移到本验收夹：仅适配移出的TEST初始化位置、真实端点索引/FIFO配置、NVIC寄存器和IsConfigured语义。另两个测试文件的回调表只是测试夹具，没有新增生产Class。

## 9. 后续由用户实现的内容

1. 自己的USBD_ClassTypeDef及设备/字符串描述符表，在启动前调用注册接口。
2. Init/DeInit中通过通用端点接口申请和释放资源，DeInit应允许复位或重复清理。
3. Setup、可选的EP0_RxReady/EP0_TxSent，处理属于Class的控制协议。
4. DataIn/DataOut和需要的IsoINIncomplete/IsoOUTIncomplete，处理本Class传输事件。
5. Class自己的流控、描述符一致性、数据生命周期及协议逻辑。

默认固件暂由迁移后的TEST兼容模块注册；接入自己的Class时，需要在外部启动组织中决定注册对象，不能让两个模块在SERVICE阶段互相覆盖。本轮没有修改Mode Manager，也没有实现这项选择或运行时切换。

保留限制：Full-Speed、单一活动Class、一个配置、固定Class内存池；第二阶段已补Bulk及非零OUT端点能力，尚未开发MSC协议。Windows实板枚举、总线时序及未来具体Class仍需实板验收。
