# STM32H747 UVC-09D Windows 实板调试复盘

**记录时间**：2026-11-01 至 2026-11-02  
**硬件**：STM32H747，USB OTG FS（PA11/PA12），通过 USB 连接 Windows 11  
**固件来源**：`codex/uvc-09c-test-pattern`，初始核对提交 `2115aa8`；本地在此基础上做 GET_CUR 兼容性修改（尚未确认已推送）  
**分支记录**：调试时正式基线为 `main @ 9ee405e`，开发基线为 `codex/uvc-09c-test-pattern @ 2115aa8`；当时 PR #3 未合并  
**路径约定**：源码均使用仓库内相对路径，不记录个人本机目录。  
**验证视频**：静态 YUY2 八色彩条，160 × 120，10 FPS；尚未接入 OV5640 实时图像

> **历史边界**：本篇记录PR #3开发阶段的实板过程。下文“未提交”“待合并”是当时快照，不是当前main状态；不要由归档文件的位置推断现分支功能。


## 面试前先回忆：并不是先知道GET_CUR有问题

| 顺序 | 当时的判断 | 依据或动作 | 后来怎样修正 |
|---|---|---|---|
| 1 | 看到OBS黑屏，可能是数据发送或画面源有问题 | 先检查Keil内部状态 | Commit=0、Alt=0、EP1没开，不能先追图像和SOF |
| 2 | 是否Windows没有发协商请求？ | 看最后一笔request不足以还原过程，改抓完整USB过程 | Windows先GET_CUR；不是完全不发请求 |
| 3 | “未SET_CUR就拒绝GET_CUR”看似合理 | 固件以probe_valid/commit_valid为门槛 | 实测Windows先读取默认参数，初次STALL阻断后续协商；这是主机兼容性问题 |
| 4 | 是否直接把valid置true就能继续？ | 审查既有授权条件 | 不能把读取能力等同正式协商，必须保留Probe确认与Commit约束 |
| 5 | 修复后有没有真正传完整帧？ | 不只看窗口亮或USB SUCCESS；检查Alt1、EP1、FID/EOF、帧CRC | 静态彩条显示，捕获样本重组33帧，38400B/帧、CRC一致；这是明确的端到端证据 |

### 面试60～90秒复述

> 我调试UVC静态彩条时，Windows能识别摄像头，但OBS黑屏。起初并不知道是GET_CUR，我先看Class状态，发现Commit还无效、Alt1没启用，说明尚未进入视频发送。我用USBPcap还原了主机请求顺序，发现Windows先读初始GET_CUR，而固件因为还没协商就STALL。修复时我没有直接把valid置true，而是让未协商的查询返回默认能力，并保持正式Probe和Commit授权独立。修改后再抓包确认Commit、Alt1、EP1都推进，最后从Payload重组33张静态彩条，长度和CRC都一致。这让我把“枚举、协商、传输、实际完整图像”几个阶段分开验收。

**追问要点：**为什么不先改摄像头？当时数据源是静态彩条；为什么不直接放行Commit？默认查询不代表协商授权；成功样本是否说明长期稳定？不能，33帧只证明该捕获样本。

以下保留原始证据与最小修复细节；分支和“尚未合并”表述均按当时阶段理解。

## 1. 现象和初始误判风险

Windows 设备管理器和 OBS 可以识别/选择名为 `UVC Camera` 的设备，但 OBS 画面黑屏。Keil 中 USB 模式处于 `RUNNING`，而 `probe_valid=0`、`probe_observed=0`、`commit_valid=0`、`interface_alt[1]=0`、`ep1_opened=0`，流状态为 `WAIT_FRAME`。

由此可知：**USB 枚举成功不等于 UVC 视频协商成功**。此时 EP1 尚未打开，不能先认定是 SOF、DCMI、SDRAM、USB 分包算法或者 OV5640 故障。本阶段使用的是 MCU 内部静态彩条，不依赖真实摄像头。

## 2. 使用的调试工具

- Keil/ST-Link：读取 MCU 内部 UVC Class 状态。
- Wireshark 4.6.9 + USBPcap 1.5.4.0：捕获 Windows 主机侧 USB URB，包括 SETUP、完成状态及 ISO 数据。为解决 Wireshark 无法显示本地接口的问题安装了 Npcap，并把 `USBPcapCMD.exe` 集成进 Wireshark Extcap 目录，最终识别 `USBPcap1`。
- FFplay 9.0.2：DirectShow 视频打开和实际彩条显示验收。

USBPcap 是**主机侧 URB 抓包**，不是可观察全部线级握手的 USB 硬件协议分析仪。

## 3. 修改前抓包：第一处可见异常

文件：`STM32H747_UVC_BlackScreen(1).pcapng`。这份捕获中，目标设备为 Bus 1 / Device 13，基础设备描述符、180B 配置描述符与 `SET_CONFIGURATION 1` 成功，`SET_INTERFACE VS Alt0` 成功。

| Wireshark 帧号 | 主机请求 | 请求长度 | 主机侧完成状态 |
|---|---|---:|---|
| 4214–4215 | Probe `GET_CUR` (`0xA1/0x81`, `wValue=0x0100`) | 34B | **STALL `0xC0000004`** |
| 4216–4219 | Commit `GET_CUR` (`wValue=0x0200`) | 1B | **STALL** |
| 4220–4221 | Probe `GET_CUR` 重试 | 34B | **STALL** |
| 4222–4223 | Commit `GET_CUR` 重试 | 1B | **STALL** |

上述请求均指向视频流接口 `wIndex=1`。在该次捕获内，未观察到 Probe/Commit `SET_CUR`、`SET_INTERFACE Alt1` 或目标设备 EP1 的视频流数据。

### 对应源代码

`CM7/Service/usb_uvc_class.c` 中，`UVC_ControlSetup()` 原先执行：

```c
case 0x81U:  // GET_CUR
    if ((selector == 1U && !ctx->probe_valid) ||
        (selector == 2U && !ctx->commit_valid))
        return USBD_FAIL;

    memcpy(ctx->control_tx,
           selector == 1U ? ctx->probe : ctx->commit,
           UVC_CONTROL_SIZE);
    break;
```

在主机首次查询时，`probe_valid=0`、`commit_valid=0`，直接拒绝 GET_CUR；ST Core / Platform 把失败落实为 EP0 STALL。**第一个明确观察到的故障发生在 EP0 UVC 控制查询阶段，不在 EP1 视频传输阶段。**

UVC 1.1 对首次 SET_CUR 前某些 GET_CUR 的初始状态不定义固定返回值，因此不能仅凭初始 STALL 就断言违反规范；但实测 Windows 的查询顺序与当前固件不兼容。

## 4. 最小修改与设计边界

**仅修改**：`CM7/Service/usb_uvc_class.c` 中 `UVC_ControlSetup()` 的两处 GET_CUR 相关逻辑。

### 4.1 未协商时允许读取默认能力

```c
case 0x81U:  // GET_CUR
{
    if ((selector == 1U && ctx->probe_valid) ||
        (selector == 2U && ctx->commit_valid))
    {
        // 已经有有效协商，返回已保存的真实值
        memcpy(ctx->control_tx,
               selector == 1U ? ctx->probe : ctx->commit,
               UVC_CONTROL_SIZE);
    }
    else
    {
        // 尚未协商，返回设备支持的默认参数
        UVC_DefaultControl(&control);
        (void)UVC_EncodeControl(&control, ctx->control_tx);
    }
    break;
}
```

沿用原函数的 `length = min(34, req->wLength)`，故主机请求 Commit GET_CUR 1B 时只返回 1B。

### 4.2 只允许真实 Probe 读取完成触发授权

在设置 `probe_reply_pending` 的判断中补充：

```c
&& ctx->probe_valid
```

确保初始默认值的 GET_CUR 只是**读取能力**，不能把它误当成一次有效 Probe 协商；`UVC_EP0_TxSent()` 原有 `probe_valid` 防护继续保留。

**没有放宽的约束**：`UVC_ProbeSet()` 的实际参数校验、`UVC_CommitSet()` 对有效 Probe / GET_CUR 完成 / Commit 数据一致性的要求、Alt1 必须 Commit 有效的条件，以及 Payload/SOF/EP1 代码均不因本次修复而改动。

这是一项基于 Windows 实测行为的兼容性修改；并非将 `probe_valid` 或 `commit_valid` 在上电时置为 true。

## 5. 修改后抓包：端到端流程打通

文件：`STM32H747_UVC_BlackScreen_ok.pcapng`，目标设备 Bus 1 / Device 15。EP0 请求与完成状态核对如下：

| 帧号 | 阶段 | 结果 |
|---|---|---|
| 2340–2345 | GET_DESCRIPTOR Device / Configuration（180B） | 成功 |
| 2348–2349 | SET_CONFIGURATION 1 | 成功 |
| 2364–2365 | SET_INTERFACE VS Alt0 | 成功 |
| 2726–2727 | **初始 Probe GET_CUR（34B）** | **成功，返回默认参数** |
| 2728–2729 | Probe SET_CUR（34B） | 成功 |
| 2730–2731 | Probe GET_CUR（34B） | 成功 |
| 2732–2735 | Probe GET_MAX / GET_MIN | 成功 |
| 2738–2741 | 第二次 Probe SET_CUR / GET_CUR | 成功 |
| 2742–2743 | **Commit SET_CUR（34B）** | **成功** |
| 2744–2745 | **SET_INTERFACE VS Alt1** | **成功** |
| 2752 起 | EP1 IN (`0x81`) 的等时传输请求 | 开始，后续捕获到有效 YUY2 数据 |

**正确理解状态机**：枚举/配置 → 读取默认参数 → Probe 提议及确认 → Commit 正式采纳 → Alt1 打开 EP1 → Application 提交一帧 → SOF 和 USB 主机调度驱动的 EP1 ISO 传输。

初始 GET_CUR 的成功**并不会自动授权发视频**；后续真正的 SET_CUR / Commit / Alt1 仍由既有状态机把关。

## 6. 验证结果

### 6.1 Windows 实际显示

用户运行：

```powershell
ffplay -f dshow -video_size 160x120 -framerate 10 -pixel_format yuyv422 -i video="UVC Camera"
```

FFplay 正确识别：`rawvideo (YUY2 / 0x32595559), 160x120, 10 fps`。用户确认窗口中八色彩条正常显示。这里10 fps是协商/格式信息，不单独当作成功输出帧率的实测统计。

### 6.2 对成功 USBPcap 抓包逐包核验

从设备 `Bus 1 / Device 15`、端点 `0x81` 的 ISO 完成 URB 中拆出等时包，再以 UVC 2B Header 的 FID/EOF 重组帧：

| 指标 | 抓包核验结果 |
|---|---|
| 完整图像帧 | **33 帧** |
| 每帧大小 | **38,400B** |
| 每帧包数 | **76 包** |
| 每帧拆包 | **75×512B + 1×152B**（都包含2B UVC Header） |
| 完整帧 CRC32 | **`FC92082C`，33/33 匹配** |
| FID | 完整帧间交替 |
| EOF | 出现在每帧末包 |
| 已解析 ISO 包状态非零数 | **0** |
| 捕获结束时未完成的下一帧 | 56 个包，不计入 33 帧 |

注：USBPcap 把多个等时包合并到 URB 中记录；Wireshark 一行不一定等于一个 512B USB 数据包。FFplay 实际显示正常与主机侧重组帧正确相互印证。

## 7. 根因总结

**直接故障原因**：设备首次 GET_CUR 前尚无有效 Probe/Commit，固件立即返回失败；Windows 在本次实测中选择先读取初始 Probe GET_CUR，收到 STALL 后未继续有效协商，导致 Commit 不成立、VS Alt1 不开启、EP1 不打开、视频不能提交。

**修复本质**：将“读取当前或默认能力”与“已经协商并授权”分开。允许 Windows 先知道设备能做什么，随后仍通过 Probe → Commit → Alt1 的严格校验。几行改动打开了后续已实现功能的入口，并没有重新实现视频收发算法。

**核心经验**：USB **枚举成功 ≠ UVC 参数协商成功 ≠ Alt1 成功 ≠ EP1 已传帧 ≠ 上层应用显示正常**。定位黑屏时必须分别检查每一层，不能由现象直接跳到图像采集或 DMA。抓包观察主机的真实请求，比只看 Keil 中“最后一个 request”更能发现第一个失败点。

## 8. 目前阶段与后续工作

- **已通过**：Windows USB 识别、Probe/Commit、Alt1、静态彩条 EP1 IN、FFplay 实际显示、成功捕获样本中的 33 帧逐字节核验。
- **尚未证明**：连续 10 分钟稳定性、关闭/打开 5 次、USB 拔插及 Reset 后恢复、更多 Windows/主机兼容性；OBS 应用本身是否恢复正常也应单独复测。
- **当时尚未完成（历史快照）**：把本地 GET_CUR 改动及配套回归测试提交到 PR #3；对旧的“未协商 GET_CUR 必须 STALL”的测试按新的初始行为更新，同时保持非法 Commit 不可被授权。
- **下一阶段**：09D 稳定性验收通过后，由用户明确批准合并 PR #3，再开始 UVC-10：OV5640 → DCMI/DMA → SDRAM/Media → UVC 视频源替换。

> **一句话复盘**：黑屏不是“发不出图像”，而是 Windows 在初始 Probe GET_CUR 就被 MCU 拒绝，导致从未进入视频发送状态；把初始查询改为返回默认能力、同时保持 Commit 授权独立，原有 USB 视频链路便顺利启动。