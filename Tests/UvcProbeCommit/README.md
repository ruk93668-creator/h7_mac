> 主线集成说明：本模块已获用户批准按软件验收结果纳入main。下文“不提交、不推送、不合并”等语句是隔离验收期间的历史约束，不代表当前授权。实际合并和远端提交号见Git历史及操作交付报告。软件测试不等于实板验收。

# 当前状态：UVC-06最终整改

本轮新增39项取消/GET_STATUS回归，原有377项保留。详细修改前后复现、逐例结果、差异补丁及两核编译见[最终整改报告](verification_report.md)。

下面为前一轮UVC-06初次实现报告，377项是当时的数量；当前完整结果以最终整改报告为准。

---

# UVC-06：UVC 1.1 Probe / Commit 验收

基线：8abc24530a7b7d55953eed797f11ee16134ea719。工作区：`C:\Users\29062\.codex\worktrees\uvc-06\h7_mac`，detached HEAD。仅在隔离工作树修改，不提交、不推送、不合并。

## 规范与实现对应

依据USB-IF原始包：https://www.usb.org/sites/default/files/USB_Video_Class_1_1_090711.zip 。`USB_Video_Class_1.1.pdf`，Revision 1.1，June 1, 2005。印刷页103–113（PDF116–126），§4.3.1.1、§4.3.1.1.1、Table 4-47/48/49。Payload版本依据同包`USB_Video_Payload_Uncompressed_1.1.pdf`。

| 依据 | 规范含义 | 本轮实现 |
|---|---|---|
| Table 4-47 | 控制数据34字节，具有固定字段偏移 | `UVC_DecodeControl` / `UVC_EncodeControl`逐字段读写；不依赖sizeof或packed结构 |
| Table 4-47 bmHint | 主机提示位；不支持的字段清零 | 只保留未压缩格式适用的D0帧间隔提示 |
| Table 4-47 输入流只读字段 | 帧大小、Payload上限、时钟、Framing由设备给出 | Probe归一到实际能力，Commit严格验证 |
| §4.3.1.1.1 | 零值留给设备协商；不支持参数回到支持值；避免增加带宽形成协商循环 | 唯一能力1/1/1000000；请求更慢帧间隔或小于512的非零Payload上限时协商失败，不能反向增加需求 |
| Table 4-48 GET_CUR | 返回协商结果；初始状态未定义；协商失败STALL | 未协商或本轮协商失败时拒绝；保留的旧Probe字节不再可读取 |
| Table 4-48 MIN/MAX/DEF | 最小、最大、默认协商值 | 唯一能力对应相同值；只读字段给出设备值 |
| Table 4-48 GET_RES | 每字段分辨率 | 当前无可调区间，所有字段步长0；后续增加多能力时必须重新设计此响应 |
| Table 4-48 GET_LEN/INFO | 数据结构长度；D0/D1均置1，其余清零 | `22 00`、`03` |
| Table 4-48 SET_CUR | 修改Probe协商状态 | 数据完成并验证后才更新，不修改已有Commit |
| Table 4-49 GET_CUR | 返回已提交值，初始未定义 | 未Commit前拒绝；成功后返回独立Commit副本 |
| Table 4-49 GET_LEN/INFO | 长度与GET/SET能力 | `22 00`、`03` |
| Table 4-49 SET_CUR | 必须来自成功Probe GET_CUR；不支持状态STALL | 完整34字节GET_CUR经EP0_TxSent后授权；Commit逐字节等于Probe且语义合法 |
| Table 4-49 MIN/MAX/RES/DEF | 未定义 | 一律拒绝，真实Core回归验证STALL |

请求必须指向VS接口1：wIndex=0001、wValue=0100（Probe）或0200（Commit）；低字节通道必须0。GET bmRequest=A1，SET bmRequest=21。SET长度仅34，GET返回min(实际长度,wLength)，拒绝零长度。GET_LEN/INFO也遵守短读上限。

## 34字节布局及设备值

| 偏移 | 字段 | 长度 | 当前值 |
|---:|---|---:|---|
| 0 | bmHint | 2 | 主机D0，默认0 |
| 2 | bFormatIndex | 1 | 1 |
| 3 | bFrameIndex | 1 | 1 |
| 4 | dwFrameInterval | 4 | 1000000（100ns） |
| 8 | wKeyFrameRate | 2 | 0 |
| 10 | wPFrameRate | 2 | 0 |
| 12 | wCompQuality | 2 | 0 |
| 14 | wCompWindowSize | 2 | 0 |
| 16 | wDelay | 2 | 0，本轮尚无视频发送流水线 |
| 18 | dwMaxVideoFrameSize | 4 | 38400 |
| 22 | dwMaxPayloadTransferSize | 4 | 512，包含未来Payload Header |
| 26 | dwClockFrequency | 4 | 1000 Hz，毫秒时基 |
| 30 | bmFramingInfo | 1 | 03（FID/EOF；未压缩格式按规范忽略此协商字段） |
| 31 | bPreferredVersion | 1 | 1，Uncompressed Payload 1.1的minor版本 |
| 32 | bMinVersion | 1 | 1 |
| 33 | bMaxVersion | 1 | 1 |

默认34字节：
```
00 00 01 01 40 42 0F 00 00 00 00 00 00 00 00 00
00 00 00 96 00 00 00 02 00 00 E8 03 00 00 03 01
01 01
```

版本三个字段全0表示主机初始协商；后续仅接受1/1/1。本轮不接受UVC 1.0的26字节控制，不添加1.5字段。

## 时间戳方案与实现边界

当前`CM7/Platform/platform_time.c`的`Platform_TimeGetMs()`直接返回HAL_GetTick()，以毫秒递增。因此声明1000Hz，后续PTS/SCR的STC值可由同一毫秒计数形成，不使用VC Header已经废弃的48MHz值。32位计数约49.7天回绕，未来接收侧按模32位解释。

未来实现必须在捕获帧与USB SOF现场采样该时基，SCR还要关联真实USB SOF计数。毫秒精度仅适合当前10FPS基础方案；本轮不生成Payload，不声称已经实现或实测PTS/SCR同步、精度或时钟漂移。后续如采用硬件高分辨率计数器，必须一起更新声明频率、PTS、SCR及测试。

## EP0处理与必要的Platform补充

真实调用链：SETUP → BSP回调 → Platform Usb_Setup → ST Core标准接口分发 → UVC_Setup → UVC_ControlSetup → USBD_CtlPrepareRx / USBD_CtlSendData。

SET_CUR：DATA OUT → HAL回调 → BSP/Platform → USBD_LL_DataOutStage → UVC_EP0_RxReady → ProbeSet/CommitSet。
GET_CUR：DATA IN完成 → Core EP0_TxSent → 标记主机已读取完整Probe。短读不授权Commit；新SETUP取消未完成控制；新Probe撤销旧读取授权。

源码审查确认官方`usbd_core.c`调用EP0_RxReady后忽略返回值，且无条件调用USBD_CtlSendStatus。仅返回USBD_FAIL不足以拒绝非法Commit。本轮必要修改`CM7/Platform/usb_device.c`的`USBD_LL_Transmit`：已STALL的EP0与其他端点一样禁止发送，且恢复EP0_STALL软件状态。Class显式USBD_CtlError后，Core的状态发送尝试不会触达BSP/HAL，不占用PIO/busy。新SETUP沿用现有清理路径解除软件STALL快照。

没有修改ST Core、HAL/LL、BSP或描述符。没有通过清除busy掩盖硬件所有权；若busy已经存在仍保持所有权，适配层拒绝提交。测试覆盖错误后的下一笔GET_LEN恢复。

## 内存与生命周期

固定池仍1024字节，Class上下文小于池容量。Probe/Commit各自34字节。EP0收发使用上下文内独立36字节、4字节对齐的持久缓冲区，只返回34有效字节；发送尾部2字节预先清零。没有栈缓冲区异步传输，没有动态malloc。编解码只访问0..33，逐字节回环及末尾哨兵验证。BSP原有PIO暂存与资源所有权机制不变。

## 修改文件

- `CM7/Service/usb_uvc_class.c`：34字节编解码、校验、协商、提交，Class请求与EP0完成回调；保留标准GET/SET_INTERFACE Alt0。
- `CM7/Platform/usb_device.c`：7行范围内的通用EP0 STALL发送保护，解决官方Core忽略RxReady错误的问题；无UVC协议依赖。
- `Tests/UvcLifecycle/lifecycle_test.c`：仅两处原NULL回调预期更新为已实现回调，40项原用例保留。
- `Tests/UvcProbeCommit/`：89项专项、执行命令、结果、两核编译日志、本报告。
- 既有Core/IRQ/Descriptor测试目录的commands.json/results.json：实际重跑生成的路径、源码SHA更新；未改测试数量或放宽断言。

## 执行及结果

在隔离工程根目录设置`PYTHONPATH=D:\32\STM32H7\h7_mac\Tests\OledAsyncAcceptance\python_deps`后：
```
python Tests/UvcProbeCommit/run_tests.py
python Tests/UsbCoreDecoupling/run_tests.py
python Tests/Ep0IrqRace/run_race.py
python Tests/UvcLifecycle/run_tests.py
python Tests/UsbUvcDescriptors/run_tests.py
```

ARMCC5 V5.06 update7 build960编译真实Class、Platform、BSP、ST Core，Unicorn执行ARM机器码；仅HAL硬件现场沿用已有Mock。完整命令见commands.json，逐例断言及失败行见results.json。

| 套件 | PASS | FAIL |
|---|---:|---:|
| 新Probe/Commit | 89 | 0 |
| 原USB Core | 204 | 0 |
| 原EP0 IRQ竞态 | 4 | 0 |
| 原UVC Lifecycle | 40 | 0 |
| 原Descriptor（含字符串） | 40 | 0 |
| 合计 | 377 | 0 |

专项75/76/88验证非法Commit/短OUT触发STALL，PIO inactive、IN不busy、未向HAL提交状态ZLP；82验证下一笔SETUP恢复。74使用真实Core完整驱动Probe SET、GET数据完成、Commit SET。

CM7、CM4均0 Error / 0 Warning（完整重编）。

Keil分别使用`UV4.exe -r MDK-ARM/h7_mac.uvprojx -t h7_mac_CM7/CM4 -o <log>`完整重编，结果见本目录cm7_keil.log和cm4_keil.log。

## 尚未验证与保留边界

- 无实板连接，不声称Windows UVC枚举、USB总线STALL波形、主机协商兼容性已经通过。
- USB Mode保持原有TEST模式，不自动切换UVC；本轮隔离测试显式注册UVC。
- EP1未打开，不实现Alt1、视频采集接入、Payload或视频传输。
- 未修改描述符、Camera、Media、DCMI、CM4源码。
- 完成软件测试不等于UVC全功能或硬件验收。PTS/SCR采样、视频延迟与带宽实测留待视频发送阶段。

正式工程main仍在8abc245，其原有16行UVC控制结构草稿未修改、未提交，也未覆盖。后续合并时需比对草稿与本轮完整结构，避免重复typedef。
