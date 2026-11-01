> 主线集成状态：用户已批准UVC-07合并main。验收代码提交为`99a77c5b2a28d8a96c8fad3efc590044547b8399`；本文下方“不直接合并main”描述的是开发阶段边界，已由此次合并授权取代。实际main及origin/main提交号以Git历史和合并交付报告为准。461项软件回归及两核完整重编结果保持有效，未进行实板验收。

# UVC-07：SET_INTERFACE与EP1 IN生命周期

基线：`3e4c0ddd3f097123224740e01dafcb0929b39e1e`。分支：`codex/uvc-07-ep1`。工作树：`C:\Users\29062\.codex\worktrees\uvc-07\h7_mac`。按AGENTS完成检查后提交并推送本分支，不直接合并main；准确SHA见Git历史及最终交付报告。

## 结果

- 原416项全部保留、全部通过；新增45项全部通过，共461/461 PASS。
- ARMCC5 V5.06 update7 build960，Keil CM7/CM4均完成`-r`完整重编，各0 Error、0 Warning。
- Class主动提交EP1数据次数为0。测试夹具主动构造12笔32字节Mock busy传输，单独标记，不是视频逻辑或实板发送。
- 默认USB Mode仍是TEST；本次测试显式注册UVC，不宣称Windows枚举、实板Alt切换或视频发送通过。

## 修改文件及边界

唯一生产源码：`CM7/Service/usb_uvc_class.c`。

1. SET_INTERFACE：VC接口0仅Alt0；VS接口1接受Alt0/1；完整检查bmRequest=01、wLength=0、完整wValue和wIndex。
2. 新增内部UVC_SetStreamAlt与提前声明。
3. Alt1必须已有有效Commit，调用USB_Device_OpenEp(81, ISOC, 512)成功后才提交Alt与ep1_opened。
4. Close失败保留原Alt与ep1_opened，新增ep1_close_pending禁止直接重用旧Alt1；必须先成功重试Alt0。
5. UVC_DeInit先关闭端点，成功后才USBD_free并清pUserData；失败保留上下文和固定池。
6. 原26/34字节说明注释保留用户最新修改，不改协议布局、编解码或Probe/Commit处理逻辑。

未修改：描述符、Platform、BSP、官方ST Core、HAL/LL、Camera、Media、Application、USB Mode、CM4源码及Keil工程配置。原416项测试主体未修改，旧套件JSON仅是实际重跑生成的命令路径和源码SHA更新。

新增`Tests/UvcEp1Lifecycle/`包含ep1_test.c、run_tests.py、commands/results/regression_results/audit.json、两核构建日志、本说明。未加入对象文件、AXF、Python依赖或临时stdout日志。

## 状态提交规则

| 操作 | 成功后 | 失败后 |
|---|---|---|
| VS Alt0→1 | 实际Open成功；alt=1、ep1_opened=true | alt=0、ep1_opened=false；Commit保留；EP0 STALL |
| VS Alt1→0 | 取消传输并Close成功；alt=0、ep1_opened=false，解除关闭故障 | alt=1、ep1_opened=true、ep1_close_pending=true；EP0 STALL |
| 重复正常Alt1 | 不重复Open，不取消已有busy传输 | 关闭故障未恢复时拒绝，不假装仍能正常运行 |
| 重复Alt0 | 已关闭时幂等成功，不多做硬件操作 | 有未完成关闭则继续重试 |
| VC Alt0 | 保持VC=0，不改变VS和EP1 | VC Alt1始终拒绝 |
| DeInit | 先关闭EP1，再归还上下文 | 不Free，不清pUserData，允许停止/释放重试 |
| Reset | 通过现有Core回调关闭EP1并释放上下文；重配后需重新Commit | 由现有Platform锁存停止要求，保留所有权，Stop恢复后重新配置 |

streaming始终false。打开端点只表示资源可用，本轮没有DataIn/SOF发送逻辑。新Probe（包括失败Probe）不会清除已有Commit；Alt0也保留Commit，因此可以不重新协商而重新进入Alt1。Reset/DeInit释放上下文后Commit自然失效。

## Close与busy所有权

Class只调用USB_Device_CloseEp，不在外部重复Abort/Flush。现有调用链：

```
UVC_SetStreamAlt(Alt0)
  → USB_Device_CloseEp
    → USBD_LL_CloseEP
      → busy时BSP_USB_AbortEp
      → BSP_USB_CloseEp
        → BSP_USB_AbortEp
        → HAL_PCD_EP_Close
```

现有忙端点路径在Platform和BSP可能各执行一次Abort，本轮未修改这些层，也未额外添加Class Abort。

- Abort失败：Core busy、BSP PIO active、原source指针继续保留。
- Flush失败：即使HAL Abort已完成，也保守保留软件所有权，等待完整清理成功。
- Abort成功但最终HAL Close失败：传输busy/PIO可以归还，但端点配置和Class Alt1仍保留，关闭故障仍需重试。
- DeInit失败：固定池与绑定继续保留；Managed DeInit可能再次清理端点，最终仍须成功重试释放后才归还Class内存。

专项16/17/18、20/21、23/24、25及37覆盖这些不同阶段。

## EP0错误状态与Core行为

SET_INTERFACE返回USBD_FAIL时，官方Core不会发送成功状态，Platform正常转为双向STALL；测试检查tx_size未被提交、EP0 IN不busy、PIO inactive、双向stalled。

额外检查发现：官方USBD_SetConfig在SET_CONFIGURATION(0)时忽略DeInit返回值，仍试图发送成功状态。Class端点释放失败且当前为该SETUP时显式USBD_CtlError；原有Platform保护阻止后续状态发送，不改官方Core/Platform。

取消配置释放失败后，Managed DeInit会置s_stop_required。Core仍把ep0_state写为STATUS_IN，但LL在停止保护处拒绝传输：HAL未收到状态ZLP，IN不busy、PIO inactive，硬件/软件端点STALL标志均保留。因此不能仅把ep0_state枚举当作实际成功发送的证明。本测试明确保留这个现有Core软件阶段行为，没有通过修改底层掩盖它。此类设备级释放故障需Stop恢复；普通Alt切换失败则可在新SETUP中重试Alt0或执行合法GET请求。

用例29/30验证失败后GET_DESCRIPTOR/GET_STATUS恢复；31验证强制补交Status仍被原Platform拒绝；34/35验证取消配置失败不得交付成功状态。

## 测试方法与覆盖

run_tests.py使用ARMCC5编译真实Class、Platform、BSP及官方Core，Unicorn执行ARM机器码，沿用原有HAL故障桩，没有修改HAL桩来掩盖问题。测试通过真实EP0 Probe SET→GET完成→Commit SET取得有效Commit，而非直接写commit_valid。

模拟器在HAL_PCD_EP_Transmit入口计数：只有测试主动构造busy时g_fixture_tx=1；其他EP1发送一律记为Class发送并判FAIL。45项中Class发送计数全部0，夹具发送累计12，用于所有权测试，详情见results.json每项ep1_transmit_trace。

覆盖：

- Alt0→1→0、参数与实际编译描述符匹配、GET_INTERFACE当前值。
- 未Commit、VC Alt1、非法接口/高字节/Alt/方向/长度。
- Open HAL_ERROR/HAL_BUSY、Abort/Flush/Close故障、保持所有权及重试。
- 重复Alt、关闭故障后必须先重试Alt0、busy重复Alt1不取消缓冲区。
- DeInit/Reset/Disconnect/Stop与取消配置，正常及失败资源回收。
- 新Probe或新Commit失败时保留原活动Commit。
- EP0失败不发成功Status，新合法请求恢复，无Class视频数据发送。

原回归：Probe/Commit89、跨请求取消39、USB Core204、EP0 IRQ4、UVC Lifecycle40、Descriptor40，合计416全部保留。

## 执行命令

隔离工程根目录，设置PYTHONPATH为本机既有Tests/OledAsyncAcceptance/python_deps：

```
python Tests/UvcEp1Lifecycle/run_tests.py
python Tests/UvcProbeCommit/run_tests.py
python Tests/UvcProbeCommit/run_cancellation_tests.py
python Tests/UsbCoreDecoupling/run_tests.py
python Tests/Ep0IrqRace/run_race.py
python Tests/UvcLifecycle/run_tests.py
python Tests/UsbUvcDescriptors/run_tests.py
```

Keil完整重编：`UV4.exe -r MDK-ARM/h7_mac.uvprojx -t h7_mac_CM7/CM4 -o <log>`。源码命令和编译器版本见commands.json；两核结果见cm7_keil.log、cm4_keil.log；全量汇总见regression_results.json。

## 保留边界

没有实板操作，不声称真实USB主机Alt切换、异常恢复或等时空端点行为通过。没有实现UVC Payload、摄像头数据发送、Alt热切换图像参数、PTS/SCR或视频吞吐测试。软件验收结论仅覆盖上述资源生命周期。本轮无需修改任何被禁止的底层文件。
