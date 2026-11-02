# UVC桥接Process验收

## 范围与调用环境

本轮仅实现CM7/Application/app_uvc_bridge.c/.h中的App_UvcBridgeProcess()，唯一公开入口。沿用用户既有三态上下文及三个统计字段，增加静态s_bridge实例和内部释放函数。未修改UVC、Media、BSP、描述符、采集模式，也未自动将入口接入App_Run。

CM7正常主循环串行调用；IRQ或PRIMASK非0立即返回，不查询、不取帧、不提交、不释放。零初始化就是IDLE，不需要初始化接口。不能在持有图像期间另行重置上下文。

## 状态与所有权

|状态/条件|行为|后续状态|
|---|---|---|
|IDLE，查询失败或can_submit=false|不获取Media图像|IDLE|
|IDLE，USB可接收但没有完整帧|等待后续调用|IDLE|
|成功取帧且160×120/YUV422/38400B/非空|保留Media借用，下一轮提交|HELD|
|取到非法帧|统计拒绝，立即尝试归还|成功IDLE，否则HELD/release_pending|
|HELD，Submit成功|记录接受借用，禁止释放|LOANED|
|HELD，Submit任何失败|USB未借用此帧，尝试归还|成功IDLE，否则HELD/release_pending|
|HELD/release_pending|只重试Media Release，不再查询、取帧、提交|成功IDLE，否则保持|
|LOANED，查询成功且frame_owned=true|保持图像，不考虑传输错误、完成计数、内部状态|LOANED|
|LOANED，查询成功且frame_owned=false|USB已归还，尝试Media Release|成功IDLE，否则HELD/release_pending|
|LOANED，正常主循环返回INVALID_STATE|活动Class已按现有契约安全销毁，尝试Release|成功IDLE，否则HELD/release_pending|
|LOANED，其他查询错误|不能证明USB已停止访问，继续持有|LOANED|

Release前先置release_pending；只有Media返回SYS_OK才清除图像地址、长度和待释放标志，进入IDLE。释放失败保留完整借用，不超时强制释放。USB尚持有时从不调用Media Release。LOANED确认归还后若Media拒绝释放，转HELD表示USB已不再借用，但Media占用尚未解除。

submitted_frames仅统计Submit接受借用次数，不代表真实发送完成；released_frames统计Media实际接收归还次数，包含丢弃帧；rejected_frames统计非法帧或Submit失败。上述计数不参与所有权判断。

## 依据

usb_uvc_class.c的USB_UVC_SubmitFrame只在PayloadStartFrame成功时接受图像，失败不借用新帧。USB_UVC_GetStreamStatus仅在status非空、非IRQ时读取活动Class；此环境下INVALID_STATE表示Class不存在。usb_uvc.h明确此前Class只在停止访问并归还资源后销毁。本入口提供固定非空输出并先检查调用环境，因此可以使用这一契约；其他错误不能当作归还证明。

CameraCapture_StreamGetFrame返回true后，消费者持有IN_USE缓冲区。CameraCapture_StreamReleaseFrame在CAPTURING、ERROR或DRAINING下受Media自身所有权检查保护；只有SYS_OK才代表Media重新接收。桥接不直接访问Media内部状态、DMA地址或USB Payload对象。

## 验收

实际ARMCC5编译正式app_uvc_bridge.c，Unicorn执行；UVC、Media和CPU上下文为可控测试桩。35/35通过，逐例结果见results.json，实际命令见commands.json。

覆盖USB未就绪不取帧、空帧、每项非法参数、Submit各类失败、Release失败重试、全部6种USB状态下仍持有图像、IO/BUSY/TIMEOUT查询失败、完成计数变化、Class销毁、IRQ和PRIMASK防护、Alt0竞态、连续两帧。获取和提交分两轮进行，验证实际传入指针和长度，提交成功后不提前释放。

原USB635次回归与实际Media72项回归全部通过，快照见usb_results.json和media_results.json。USB/Media正式源码SHA在preservation.json中保留。

复现：

```powershell
python Tests/UvcBridge/run_tests.py
python Tests/UvcUsbCleanup/run_regression.py
python Tests/CameraContextRefactor/run_tests.py
powershell -NoProfile -ExecutionPolicy Bypass -File Tests/UvcBridge/build.ps1
```

## 集成边界

本轮未自动调用Process。后续在CM7 App_Run中由用户接入，并持续运行既有CameraCapture_Process和USB_UVC_Process；无需在桥接中重复调用它们。仅配置UVC且完成Commit/Alt1后才会获取图像；TEST模式不会取图。保持唯一帧提交者和唯一Media消费者，不允许其他模块在桥接持有期间释放相同帧或将其再次提交。

未操作实板，不证明真实OV5640 YUV422字节顺序、DMA/USB并发、Windows视频显示或长期稳定性通过。测试桩验证的是接口契约与桥接逻辑，不能代替硬件验收。
