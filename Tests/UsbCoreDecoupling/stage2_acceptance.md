# USB 第二阶段：通用端点与生命周期验收

基线：`ec22e00a5756567f28f3f7934fda23e6ac1ac327`。以正式工作区源码实施，未使用归档工程。

结果：**192/192 PASS，2500 项断言；CM7 Keil：0 Error、0 Warning。** 本轮未连接实板 USB，不代表 Windows 枚举、等时吞吐或未来 UVC/MSC 协议验收通过。

## 1. 原问题和证据

| 原实现 | 问题 | 本轮处理 |
|---|---|---|
| Platform 的 ISO incomplete 同时写全局 TIMEOUT | 普通丢包通知可能被上层当成设备故障 | 清对应端点 busy、记录端点 TIMEOUT，经官方 Core 通知 Class，不写全局 TIMEOUT |
| Reset/Disconnect 由 ST 调 DeInit，Platform Stop 也直接调 | 无法确认是否 Init 成功，重复调用同一生命周期的 DeInit | 包装外部 Init/DeInit，跟踪活动、释放失败及剩余端点清理状态 |
| USB_Device_Free 立即清占用标志 | DeInit 先 Free 后返回失败时，旧 Class 可能仍持有内存 | DeInit 期间只登记释放意向，确认回调和端点清理成功后提交 |
| 官方 USBD_Stop/USBD_DeInit 忽略部分下层返回值 | 硬件未停或 Class 未释放时可能继续销毁状态 | LL Stop 锁存失败，生命周期包装拒绝释放；LL DeInit 保留注册、内存及外部绑定 |
| BSP 端点和 FIFO 固定为 EP1 IN ISO512 | 其他 IN、非零 OUT 和 Bulk 无法使用 | 按 FS 规则校验，注册时读取外部端点需求并由 BSP 一次规划 FIFO |
| HAL_PCD_EP_Open 先修改 PCD 上下文再检查 HAL 锁 | HAL_BUSY 后残留 maxpacket，后续 FIFO 规划误认为仍占用 | BSP 保存并回滚本方向 PCD 上下文 |

另外，PCD反初始化失败时原LL分支未恢复已被官方清空的pDesc；用例359复现NULL描述符，修正后能Stop恢复并重新枚举，记录见 `stage2_deinit_failure_before_fix.json`。

HAL打开失败问题保留了真实失败记录：`stage2_open_failure_before_fix.json`。用例358在回滚前读到 `maxpacket=64`，预期为0；修正后通过。HAL桩按当前官方函数的写入与加锁顺序注入 BUSY，未修改官方 HAL。

## 2. 修改范围

| 文件 | 修改内容 |
|---|---|
| CM7/Platform/usb_device.c | 生命周期包装、池释放提交、ISO通知分级、外部端点描述符需求提取、共享RX Flush保护 |
| CM7/Platform/usb_device.h | 更新接口说明，公开接口签名保持不变 |
| CM7/BSP/bsp_usb.c | 双向EP0至EP8、FS包长校验、端点上下文回滚、FIFO预算和配置、非零OUT接收 |
| CM7/BSP/bsp_usb.h | 增加端点能力常量、BspUsbEndpointConfig_t及BSP_USB_ConfigureEndpoints |
| Tests/UsbCoreDecoupling | 保留原用例，扩展桩与60项测试，输出结果、命令、源码审计与本报告 |

未改 TEST Class、Mode Manager、UVC Class/描述符、ST Core、HAL、LL、CM4、相机、DCMI、SDRAM或Media。Keil工程文件也未修改。用户原有 `uvoptx` 修改、`framework/` 删除和 `output/`、`tmp/` 不纳入本轮提交。

## 3. 架构与端点责任

```text
Application / Mode Manager（保持原实现）
                │
外部 Class + 静态 Device / Configuration Descriptor
                │ 注册与申请端点
                ▼
Platform usb_device.c
  注册校验、描述符缓存、生命周期包装、池、端点状态
                │
ST Device Core（原版）
  标准请求、EP0状态机、向外部Class分发
                │ USBD_LL_*
                ▼
BSP USB → HAL PCD / LL → USB2 OTG FS
  硬件能力、FIFO、GPIO、HSI48/CRS、PIO、中断
```

EP0由官方Core及其LL适配管理，固定双向Control64。Class不能用通用数据端点API打开、关闭或提交EP0数据；控制请求仍使用官方 `USBD_Ctl*` 接口。

非零端点的地址、类型、最大包长、间隔和提交节奏由Class决定。Class调用Platform通用接口，不直接操作BSP或HAL。Platform不解析UVC/MSC类专用内容。

## 4. BSP实际支持范围

| 项目 | 支持能力 |
|---|---|
| 地址 | EP0至EP8，IN/OUT；保留地址位及EP9以上拒绝 |
| EP0 | Control，最大包长64；Transmit保留0x00作为0x80的原兼容别名 |
| Bulk | 非零IN/OUT；最大包长8、16、32或64 |
| Interrupt | 非零IN/OUT；最大包长1至64 |
| Isochronous | 非零IN/OUT；最大包长1至1023 |
| 非零Control | 不支持，返回NOT_SUPPORTED |
| IN传输 | ISO单次不超过最大包长；Bulk/Interrupt支持多包，不再限定512字节 |
| OUT接收 | 非零端点按整包容量提供缓冲；ISO一次提供一包容量 |
| 实际接收长度 | GetRxSize读取指定OUT端点的xfer_count，可小于提供的容量 |
| STALL | 按已打开的实际类型判断；等时端点不支持HALT |
| Close | 只关闭指定方向；失败保留资源，成功才清opened/busy |
| Flush | busy不冒充取消；OUT共享RX，存在任一OUT busy时拒绝Flush |

非零传输同时限制硬件XFRSIZ/PKTCNT：不超过 `0x7FFFF` 字节和1023包。Bulk64单次最多65472字节。接收容量为MPS整数倍，避免HAL向上取整接收量后超出缓冲；例如未来接收31字节数据时，可提供64字节缓冲，再读取实际31字节。

IN发送缓冲在完成、关闭成功或确认停止前必须保持有效。当前仍为PIO、USB DMA关闭；本轮没有新增缓存维护、DMA缓存或分包调度器。

## 5. FIFO依据及分配

硬件依据：[STM32H747xI/G数据手册DS12930](https://www.st.com/resource/en/datasheet/dm00602212.pdf) 的USB章节：USB2支持含EP0在内的9组双向端点，专用FIFO RAM为4KB，即1024个32位Word。没有套用其他STM32型号的320Word总容量。

当前驱动依据：

- `stm32h7xx_hal_pcd_ex.c` 的SetRxFiFo/SetTxFiFo以Word为单位；TX FIFO编号对应IN端点编号，后续起始地址依赖前面FIFO深度。
- 未使用的低编号TX FIFO按驱动要求保留至少16Word。
- 当前 `PCD_WriteEmptyTxFifo()` 只有空闲Word数足以装下一包才写入；当前PIO ISO还会直接写整包，不能给1023字节端点只分配64字节FIFO。
- 所有OUT共享一个RX FIFO；OUT Close和IN Close操作不同端点寄存器，OUT Flush则确实清空共享RX。

固定部分：RX=320Word，TX0=64Word，TX1至TX8最少各16Word。RX按1023字节最大OUT包及9组端点状态预留，超过当前保守计算的289Word下限。

```text
TXn = max(16, ceil(本Class各备用设置中该IN端点最大包长 / 4))
总量 = 320 + 64 + Σ(TX1 ... TX8) ≤ 1024 Word
```

| Class端点需求 | TX分配 | 总Word | 总字节 |
|---|---|---:|---:|
| TEST只有EP0 | TX1至8各16 | 512 | 2048 |
| EP1 IN ISO512 | TX1=128，其余各16 | 624 | 2496 |
| 8组双向Bulk64 | TX1至8各16 | 512 | 2048 |
| 4个IN ISO512 | 对应TX各128，其余各16 | 960 | 3840 |
| 2个IN ISO1023 | 对应TX各256，其余各16 | 992 | 3968 |

5个IN ISO512需1072Word、3个IN ISO1023需1232Word，均在写寄存器前返回NO_RESOURCE，保留旧有效布局与注册对象。

注册流程为：校验外部描述符 → 提取通用Endpoint Descriptor → BSP完整预算 → 配置全部FIFO → 提交Class注册。运行中不重排；Stop成功、端点释放后可恢复布局或注册其他Class。FIFO硬件写入失败会标记布局无效，禁止Start；Stop恢复成功后再允许启动。

本方案保守累计所有备用设置的各端点最大需求，不做互斥配置的空间复用。Class必须在连接期间保持配置字节不变。EP1 ISO512能力保留，但必须先登记含相应需求的配置；默认TEST不会预留512字节数据包FIFO。

## 6. ISO事件与全局错误

```text
HAL ISO incomplete → BSP → Platform
  清对应端点busy、last_result=TIMEOUT
       → 原版USBD_LL_IsoINIncomplete / IsoOUTIncomplete
       → 当前Class可选ISO通知回调
```

普通ISO未完成不会修改全局诊断，也不会抹掉已有真实错误。真实HAL失败、官方Core返回分发失败或释放失败仍被记录。非等时、已关闭端点和异PCD的ISO伪通知被忽略。

当前官方Core忽略ISO通知回调自身的返回值；本轮保持这一行为，不把它描述成能传播Class回调错误。DataIn/DataOut的错误分发仍沿用原实现。

## 7. 生命周期释放顺序

内部状态：INACTIVE → INITIALIZING → ACTIVE → DEINITIALIZING。外部DeInit失败进入RELEASE_FAILED；外部已成功但剩余端点关闭失败进入RELEASING。

- **Reset**：清旧EP0打开快照 → 官方Core复位并调用包装DeInit、重建EP0 → 继续清理残留非EP0 → 核对两个EP0真实opened。当前官方Reset在Class失败时仍尝试打开EP0，并非提前返回；它忽略OpenEP返回值，因此仍须核对。
- **Disconnect**：检查官方断线返回值 → 释放当前生命周期 → 清理数据端点。Class释放失败会锁存恢复要求。
- **Stop**：确认BSP硬件停止成功 → 包装DeInit → 清剩余端点 → 提交池释放 → 恢复FIFO布局及Core状态。硬件停止失败时不调用外部DeInit、不释放内存。
- 已成功释放的同一生命周期不会因后续Reset/Disconnect/Stop再次调用外部DeInit。外部失败允许重试；仅端点清理失败则只重试端点清理。
- Init和DeInit回调内部再次调用Init/Register/Start/Stop返回BUSY；失败后禁止Start/Register，允许主循环重试Stop。

官方 `USBD_Stop()` 忽略LL停止错误并返回OK，这是官方实现的既有行为。Platform会保留真实失败和所有权；应用应使用返回可靠结果的 `USB_Device_Stop()`。官方 `USBD_DeInit()` 清掉描述符和pUserData后，LL适配也会在释放失败时恢复仍需使用的绑定。

## 8. Class固定池边界

- 单块1024字节，32字节对齐；合法申请1至1024，重复申请或非法长度返回NULL。
- 只接受与基址完全相同的Free；NULL、内部指针、尾指针无效；同一分配周期重复Free安全。
- DeInit回调内Free只登记意向，回调返回失败时保留原分配标志；外部不能在失败态强行Free掩盖占用。
- 外部成功且剩余端点清理成功后才提交Free；若端点清理失败，保留池并重试清理，不重复成功的外部DeInit。
- 外部DeInit成功但没Free：报告IO、保留池、禁止重新注册和启动，不静默回收。修正Class遗漏后，由持有合法基址的清理代码完成Free，再重试Stop。
- Init失败也可能持有部分资源，DeInit须能处理本Class的部分初始化。Class仍负责自身资源和外部指针，Platform不会猜测协议内部资源。

## 9. 测试、命令与编译

```powershell
python Tests/UsbCoreDecoupling/run_tests.py

D:\32\UV4\UV4.exe -b D:\32\STM32H7\h7_mac\MDK-ARM\h7_mac.uvprojx -t h7_mac_CM7 -o D:\32\STM32H7\h7_mac\Tests\UsbCoreDecoupling\cm7_keil.log
```

编译器：ARMCC5 5.06 update7 build960，C99。模拟测试编译当前正式BSP、Platform、TEST、Mode、Framework Init和未改的ST Core，在Unicorn中执行ARM指令。FIFO测试额外执行未改的官方HAL PCDEx SetRxFiFo/SetTxFiFo，核对实际寄存器的起始Word与深度。

| 分类 | 数量 | 结果 |
|---|---:|---|
| 原基础枚举 | 52 | PASS |
| 原Mode回归 | 32 | PASS |
| 原Core回归 | 48 | PASS |
| 新增通用端点/生命周期 | 60 | PASS |
| 合计 | 192 | 192 PASS，0 FAIL，2500断言 |

原用例41仍检查非法包长，将已变合法的64换成1024；原用例210由旧BSP“不支持Bulk/其他IN”改为检查新增能力成功。没有删除原用例；非法类型和长度的覆盖在新增326/344等用例中保留并扩大。

每项输入、预期、实际状态和Pass/Fail见 `results.md`、`results.json`；精确编译链接命令和输出见 `commands.json`。`source_changed=[]`表示测试运行过程中未改源码，并非本任务没有修改源码。

正式Keil CM7：**0 Error、0 Warning**。CM4本轮未重编；源码和Target配置均核对不变，不声称本轮CM4编译通过。

## 10. 范围审计及待验证项

`source_audit.json`记录1144个受保护文件的SHA256，全部不变；CM4 Target及整个uvprojx与基线一致。BSP既有时钟、GPIO/NVIC初始化和IRQ入口另外核对未改。

尚未验证：Windows实板枚举、实际多端点总线并发、ISO最大包持续吞吐及电气时序。仍只有一个活动Class和一个配置；未实现UVC、MSC、运行时模式切换、复合设备、动态FIFO或RTOS。

Mode Manager源码保持原样；其RUNNING分支当前不主动处理全局错误，等待枚举阶段才检查相关诊断。本轮只保证正确记录真实错误，不扩展其故障状态机。
