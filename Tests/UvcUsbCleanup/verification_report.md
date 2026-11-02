# UVC-10A 软件整理与契约验收

2026-11-02。基线为09C分支24a633f，保留原工作树实板兼容性修复，独立分支codex/uvc-10-usb-cleanup。未合并main，不修改用户的原工作树。

## 修改及保护

|文件|本轮变化|
|---|---|
|CM7/Service/usb_uvc_class.c|保留未提交GET_CUR修复；公开函数提前；内部声明集中；传输常量命名；展开密集语句、补充状态注释|
|CM7/Service/usb_uvc.h|整理声明次序；逐项明确上下文、输入、错误、借用、取消与归还契约；不改签名/枚举/字段|
|CM7/Service/usb_uvc_payload.h|明确软件分包与硬件Abort的边界；不改Payload实现|
|CM7/Service/usb_uvc_desc.c/.h|修正过时职责注释，说明静态描述符所有权；C token/字节数据不改|
|CM7/Service/usb_mode.c/.h|明确冷启动、RUNNING、同模式重连和主循环约定；C实现token不改|
|Tests/UvcProbeCommit/probe_test.c、run_tests.py|仅更新78、83两项与实板行为冲突的预期，保留编号并增加默认内容/不授权断言|
|Tests/UvcUsbCleanup/|基线/最终完整执行记录、新14项契约测试、源码审计及本报告|
|Docs/Debug/UVC-10_USB接口设计.md|架构、5个API、生命周期、所有权、恢复和未来调用边界|
|既有相关commands/results及Keil日志|实际重跑更新，不修改其他测试主体|

原始本地兼容修复文件SHA256：db90efa9281f6792abaf8dffb06695c0db1c1fb25152f9e881fa7ef0db14f39a。27个Class函数体去除注释/空白并展开新命名常量后，与这个本地已验证版本的C token逐项相同；不是拿远端旧文件覆盖修复。见cleanup_audit.json。

审查后保持Payload.c、BSP、Platform、HAL/LL、ST Core、CM4、Application、相机/Media/SDRAM及Keil工程配置不变。没有新增桥接文件、CameraCapture调用或Media回调。静态彩条和TEST内存优化均继承09C，未重新实现。

## 基线问题证据与处理

复制实板修复后，旧645次测试实际运行结果643 PASS、2 FAIL，见baseline_results.json（完整输出保留）：

|用例|旧预期|实际行为及依据|处理|
|---|---|---|---|
|Probe78|初始Probe/Commit GET_CUR必须失败|实板Windows先查询初始值；本地修复返回默认34B或按wLength截断，flags不授权|改为逐字节核对默认值、有效长度、probe_valid/probe_observed/commit_valid不被设置|
|Probe83|失败Probe后GET_CUR必须失败|本地修复返回默认能力，但不能继承读取授权或覆盖旧有效Commit|核对返回默认数据、probe_valid/observed/reply_pending为false、原Commit保持不变|

这是旧预期与已实板实现不一致，不是通过放宽非法参数或伪造有效标志绕过失败。GET_CUR成功只提供能力；真正Commit仍要求有效Probe、完整GET_CUR成功结束、34B参数一致，Alt1仍要求有效Commit。非法Commit请求、短包、26B SET_CUR、跨请求取消和资源保护断言全部保留。

## 最终完整软件回归

|套件|执行数|PASS|FAIL|
|---|---:|---:|---:|
|USB Core|204|204|0|
|EP0 IRQ竞态|4|4|0|
|UVC Lifecycle|40|40|0|
|Descriptor/字符串|40|40|0|
|Probe/Commit|89|89|0|
|EP0取消/GET_STATUS|39|39|0|
|EP1 Lifecycle|45|45|0|
|Payload 62项×O0/O2|124|124|0|
|09B Streaming|36|36|0|
|09C静态图像|24|24|0|
|10A新增契约|14|14|0|
|总计|659|659|0|

新增14项逐例见results.json：初始Probe完整/短读/超长接收上限、Commit1B/34B、Windows实际请求顺序后正式协商开流、初始GET被其他SETUP打断、失败Probe后默认查询不授权、取消Abort失败持帧、Incomplete不能伪造完成、IRQ拒绝调用、Class释放后查询不写输出、Reset与DeInit失败继续可查询所有权直至Stop成功。

保持原645次全部执行，无删除、跳过或修改结果统计。Tests/UvcUsbCleanup/run_regression.py逐项执行11个套件，检查各自执行数量及实际PASS数，不以控制台字符串替代结果。新增请求序列验证的是成功抓包中的请求顺序与本工程能力，硬件HAL仍是桩，不把模拟回放当成再次实板开流。

## Keil完整重编

ARMCC5 V5.06 update7 build960，C99。执行现有Tests/UvcTestPattern/build.ps1，临时工程仅添加UVC宏，不修改正式uvprojx；最终保留TEST配置产物。

|配置|Error|Warning|Code|RO|RW|ZI|日志|
|---|---:|---:|---:|---:|---:|---:|---|
|CM7 UVC|0|0|65314|2558|668|55396|Tests/UvcTestPattern/cm7_uvc_keil.log|
|CM7 TEST|0|0|64534|2526|660|17004|Tests/UvcTestPattern/cm7_keil.log|
|CM4|0|0|20334|914|636|2580|Tests/UvcTestPattern/cm4_keil.log|

日志SHA、生产文件SHA及受保护目录检查见cleanup_audit.json。原Payload纯软件测试仍沿用没有.framework_init输入段的测试scatter，模拟链接日志中的L6314W属于既有测试链接提示；上述正式Target均0 Warning，没有关闭编译器告警。

## 复现与实际限制

```powershell
python Tests/UvcUsbCleanup/run_tests.py
python Tests/UvcUsbCleanup/run_regression.py
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/UvcTestPattern/build.ps1
```

测试在独立工作树运行，Unicorn执行真实ARM代码；只替换外设和主机事件。没有新建RTOS、malloc、USB协议栈或图像层。原main/09C工作树的用户源码、工程配置及output/、tmp/均保留。

没有发现需要改变现有所有权算法的新缺陷；完成了失败不归还、停止成功才销毁Class及迟到完成保护的针对性确认。现存限制包括合法主循环与IRQ同名INVALID_STATE需由调用契约区分、主循环饥饿、有限HAL轮询、固定能力、Token耗尽及单一图像提供者约束，详见接口文档。

用户此前实板成功证据及33帧CRC记录保留在09D复盘，本轮直接读过成功pcap中的初始GET_CUR与Alt1完成状态。本轮整理后的二进制尚未实板复测；10分钟、5次开关、拔插/Reset、OBS等稳定性仍待用户验收。本分支推送并创建依赖09C的审核PR，不自动合并任何分支。
