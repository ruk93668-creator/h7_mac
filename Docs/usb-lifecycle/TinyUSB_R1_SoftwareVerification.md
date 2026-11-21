# TinyUSB R1软件验证

VERIFIED（软件）：python Tests/TinyUSBReplacement/run_stack_tests.py，原12项、1679断言全部通过，实际TinyUSB Core/MSC与原RAM backend，原fixture DCD为协议边界，不是H7硬件。

VERIFIED（真实DWC2负向）：run_r1_tests.py执行修改后真实Core、MSC、DWC2，初始资源状态及MMIO由测试输入。2项、33断言通过：新IN/OUT入口实际返回FAILED；保留DCD buffer、BUSY/CLAIMED；重复Abort拒绝；Release/Xfer/DeInit/Init/Reset不越过故障；晚到Core XFRC不能退役；MSC Reset失败保持DATA状态。没有Mock返回SUCCESS，没有写PROVEN或清资源。两项不是取消成功验收，MMIO模型未推进硬件完成。

VERIFIED：run_dwc2_tests.py重新运行原8观察，六个不返回反例仍不返回，证据已保存。100000指令预算仅防测试宿主挂死，不是固件硬件超时。本轮尚未把六项转为有界失败，验收FAIL/BLOCKED。

VERIFIED：compile_probe.py三个profile共18对象编译0错误、57警告，脚本返回失败；沿用已知FreeRTOS接口/enum警告。不是三份完整固件、不是0Warning。新增源码由ARMCC5实际编译并在测试AXF链接。

NOT_TESTED/未完成：真实取消SUCCESS、NAK/Disable成功后共享RX消费证明、正常完成/取消竞争的DCD事实退休、ISR抢占、FIFO/Core Reset有界失败、EP0失败传播全集、恢复后新Session、实板全部。不能把Core注入的晚到事件测试扩大为真实DCD迟到IRQ已验证。

报告：reports/r1.json、r1-compile.json、stack.json及history、dwc2.json及history、compile.json及history。既有历史FAIL不删除、不改断言制造通过。
