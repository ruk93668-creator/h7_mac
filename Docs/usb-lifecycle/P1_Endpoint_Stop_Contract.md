# P1-C2：双向Endpoint协调契约（局部实现）

VERIFIED（源码/软件）：Platform保留旧IN路径；只有真实BSP的EP0 OUT快照确认C1成套绑定后，Usb_StopDevice才进入Usb_DrainBulkEndpointStops。没有基于普通Busy猜测绑定，也没有自动Enable。默认固件仍未启用C1，此处不是完整P1闭环。

## 当前实现

- 同一UsbTask经现有Mode→USB_Device_Stop推进；Controller意图只分配一次，IN/OUT各自独立Transfer/StopAttempt；16个方向槽记录地址、真实阶段、身份、注册/归还状态和错误。
- 先发布数据端点提交Fence，再遍历全部方向。新Open/Transmit/Receive拒绝，不通过同步Abort覆盖；每轮每槽最多一次有界推进，单方向错误不短路其他方向。
- 先核对全组，外部已经建立的Attempt不自动收编；返回普通Busy且registered_pending=false，不伪装成已提交取消。
- IN和OUT分别使用原真实Begin/Poll/Commit及硬件证据。未Arm也通过真实停止原语，而不直接把Idle/busy=false当作Quiescence Proof。
- GetStopProgress保留原字段布局并追加全组快照、first_error、pending_count、submission_fenced、endpoint_group_proven、controller_gate_blocked。每条stage按方向解释；快照仅诊断，不是外部释放授权。
- 全组USB proof闭合后，**仍返回NOT_SUPPORTED/RECOVERY_REQUIRED**：尚无已批准EP0和消费者PreparedTicket。保持Class、Controller和全局Fence，不调用BSP Stop，不声称完整Stop成功。

VERIFIED：独立测试11项实际执行HAL/LL、BSP、Platform；模型只推进真实MMIO事件，未向软件proof写PROVEN。覆盖双向Pending、Timeout独立收尾、Reset/Disconnect、提交Fence、普通Busy、ID耗尽、外部Attempt以及重复Stop。报告Tests/UsbLifecycleFinal/reports/dual.json含每一步实际/预期、MMIO写入、源哈希和断言；初轮夹具失败另存dual-history，不删除。

VERIFIED：第一轮出现错误的无中断源强制进入IRQ及Busy常数夹具错误；修正为只有实际GINTSTS&GINTMSK为真才触发IRQ，保留真实OUT与Reset组合，未修改BSP对未证明GRSTCTL竞争的保守故障处理。初轮证据保留。单纯手动调用空IRQ不是正常外设中断源。

BLOCKED：EP0合法停止、AHB定时ZLP接口、Class/Worker/Frame ticket整合、整机Resume与自动C1启用。新组件只形成端点票据，不把它当Class或IDMA静止证明；不新建Task/动态内存。不具备main合并资格。
