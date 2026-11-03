# ffplay无法启动图：从“设备被占用？”定位主机带宽分配失败

记录日期：2026-11-03。**状态：失败阶段和主机返回原因明确；尚未记录换USB拓扑或更小包长后的成功取流。**

## 真实起点

设备已能以Roxy Camera被DirectShow发现，但执行ffplay报：

```text
Could not run graph (sometimes caused by a device already in use by other application)
```

起初不能确定是占用、能力不匹配、UVC协商、Alt1开启还是后续发送失败。“sometimes”只是可能原因，不能当成结论。没有记录“关闭其他程序后就恢复”的成功结果。

## 怎么一步步缩小范围

| 动作 | 证据 | 如何改变判断 |
|---|---|---|
| 先按层区分错误 | 本次是Run Graph失败，不是之前的Could not set video options | 查流启动，而非直接归因于枚举、模型或图像采集 |
| 按VID/PID锁定本板 | 抓包中1209:0001，本次地址64 | 避免把Hub、调试器或其他摄像头当成本板 |
| 检查实际控制协商 | Probe GET/SET与Commit的控制传输成功 | EP0已能工作，但还不能证明视频已经开始 |
| 解码协商数据 | 38400B、dwFrameInterval=400000、最大Payload1023B | 实际协商为160×120 YUY2、目标25 FPS，先前10 FPS缓存假设不能解释这次失败 |
| 找第一笔失败 | 3095/3096：选择VS接口1 Alt1，返回0x80000700 | 直接证据指向主机带宽分配 |
| 检查失败后的动作 | 3097/3098退回Alt0成功；3105/3106重试仍失败 | Alt0是失败后的清理动作，不是首次原因 |
| 查是否已传图像 | 没捕获到本板EP1等时图像传输 | 此次还没进入有效帧发送，不能用Media帧率解释这笔失败 |

## 核心证据

文件：`STM32H747_UVC_BlackScreen_no.pcapng`；本次读取版本410508B，修改时间2026-11-03 21:50:43。该文件在过程中被覆盖过，帧号只对应这一版本。

| 帧号 | 事件 | 完成状态 |
|---|---|---|
| 3091～3092 | Probe GET_CUR | SUCCESS；34B |
| 3093～3094 | Commit SET_CUR | SUCCESS |
| 3095～3096 | VS接口1，Alt1 | **USBD_STATUS_NO_BANDWIDTH，0x80000700** |
| 3097～3098 | VS接口1，Alt0 | SUCCESS |
| 3103～3104 | 重试Commit | SUCCESS |
| 3105～3106 | 再次Alt1 | **NO_BANDWIDTH** |

USBPcap记录的是主机URB；SELECT_INTERFACE可在主机资源分配阶段失败，不应据此保证真实SET_INTERFACE已发到STM32。也不能把状态误叫成“MCU返回STALL”。

## 为什么软件算得过，Windows仍拒绝

软件预算：每包1023B含2B头，图像载荷1021B；38400B需38包，目标25 FPS的帧周期40ms，理论提交次数足够。

主机还要为Alt1声明的1023B/1ms端点预留周期总线时间。Hub事务转换、同控制器其他周期端点和拓扑都可能影响资源。软件包数预算不是主机带宽分配成功保证。

**仅把output_fps从25改10，不一定降低预约需求**：若Alt1仍声明1023B、bInterval=1，主机仍按该端点能力分配。

## 提出的后续对照，不写成已执行修复

1. 电脑端口直连，避开扩展坞；减少其他摄像头/音频周期流，检查是否仍共用同一控制器或Hub。
2. 若仍失败，考虑512B、10 FPS的独立兼容性固件，同时核对描述符、Probe/Commit、OpenEP和发送参数；不能只改一个数字。
3. 重抓Alt1成功与EP1数据后，再验证完整帧、FID/EOF和真实FPS。

这几项尚无成功实板结果，当前不能说“已通过降低包长修复”。

## 面试60秒复述

> USB枚举恢复后，ffplay仍报Could not run graph，提示可能被占用。我没有把提示当根因，而是抓取从启动取流到失败的完整控制过程。按本板VID/PID筛选后，发现Probe和Commit都成功，真正第一笔失败是切换Alt1，Windows返回NO_BANDWIDTH，之后才回退Alt0。协商是38400字节、目标25 FPS、1023字节Payload。这个证据让我把问题从设备内部状态机转向主机端周期带宽分配，也理解了平均图像带宽和端点每毫秒预约带宽是两回事。后续换拓扑和小包长对照仍需实测，不能提前宣布修复。

面试追问：Alt0为什么成功？它释放周期带宽；为什么不先改Media？本次没有开始EP1图像传输；25 FPS是否实测？不是，是协商目标。

[Microsoft：USB带宽分配与Alt设置](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/usb-bandwidth-allocation)
