# 生产路径问题与未解决项

## P1 / C：USB失败STOP后台完成后没有提交RELEASE

源码基线：cc9f25a54d95cdfbe332c1c5f7a4791ff06e68d3；本轮未修改生产代码。

调用链：真实App_USBHandleCommand(STOP) → UVC Cancel返回IO并保留帧 → App_USBFinishRound报告该STOP失败并清除stop_command → 下一轮Cancel恢复 → Bridge Return → CameraTask实际Media归还 → Return ACK → USB_Mode_Stop成功 → started/stopping清除，但没有App_CameraConsumerEnd。

源码位置：`CM7/Application/app_usb.c:178` App_USBFinishRound；201–208行只有stop_command为真且结果非BUSY才进入ConsumerEnd/ACK块。首次失败已在208行清除了stop_command；后台成功不会再次进入205行的ConsumerEnd。此时USB业务state=0、帧均已归还，但资格仍ACTIVE。正常STOP路径则进入RELEASING，等待真实证据。

复现证据：`Evidence/7.json.gz`：CameraCapture_StreamReleaseFrame实际归还；Return ACK事件4533；USB_Mode_Stop事件4620/4621成功；后续多轮仍没有USB角色的RELEASE请求。轨迹中App_CameraConsumerEnd的Storage角色调用不能当作USB释放。`contracts.json` ID7记录首个未满足因果义务。C检查181行发现Camera资格未进入RELEASING。对应旧B2 UVC/ID10的Abort失败后后台重试路径亦受影响，旧C断言没有检查资格释放，不能凭其通过掩盖此缺口。

安全影响：并未观察到强制释放帧或提前停止硬件；相反，资格残留可能阻止最后一个消费者退出以及后续重新START。没有证据把它归类为内存破坏。

建议最小整改（未实施）：将“背景停止真正完成后需要提交一次ConsumerEnd”的责任与“原STOP ACK是否仍待回复”的标志分离；保留原失败ACK/FAULT，不以后台恢复改写原结果，且只在Bridge真实归还后登记释放意图。需要单独授权生产整改，不能用本测试任务或C2.2C真实归还证明实现替代。

## 既有 Consumer 并发冷启动专项仍FAIL

`Evidence/regression/consumer.json` ID17 O0/O2仍失败，结果42/44；两个消费者同时首次QUERY的场景没有全部获得资格。`CM7/Application/app_camera_qualification.c:274`使用Qual_OtherWork门控首次会话分配；该门控与双路同时QUERY交错有关。此处保留为既有未解决失败，不在本轮重写协议或据此宣称并发首次启动可用。本联合11项按已指定的USB先获权、Storage随后获权执行，不覆盖该交错。

## D：128项历史B2/B3完整谓词证据仍不足

全部153个原C入口本轮执行成功、授权前置已补齐；这不能替代原Python对Debug、结果、ACK/FAULT及Task交错的完整判定。原完整门禁24项仍可确认，129项未自动提升：其中1项有上述C类相关根因，128项仍BLOCKED/D。每项的原始/候选轨迹、调用计数、首个旧轨迹差异和证据缺口见Legacy153Review.md及legacy_153_gate.json。

## C2.2C真实关闭尚未完成

真实App_Camera资源Ops的released/idle仍NULL（app_camera.c:629–634）；正常业务STOP后资格保留，不能生成QUALIFICATION_RELEASED或HARDWARE_STOPPED。本轮未实现任何生产归还证明、未强停Camera、未清空Replay、未接入C2.2C。8项核心桩通过只证明门控契约，不证明真实关机闭环。
