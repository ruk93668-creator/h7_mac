# V1 实板后续验证

本轮未操作实板。用户此前确认的 V1 七项功能通过属于历史记录，不冒充本轮执行。以下产品级项目均 HARDWARE_REQUIRED，执行状态 NOT_RUN；不计入软件单元 128 项 PASS 数。

测试必须记录 CM7/CM4 固件 SHA256（Resources.json）、完整断电条件、操作时间、Session/Request/Generation、Consumer phase/grant、Lease/Return/ACK、硬件调用计数、文件解析结果和错误现场。不得改写私有状态模拟资格成功。

| ID | 操作与前置条件 | 观察与通过条件 | 当前状态 |
|---|---|---|---|
| H01 | V1 完全断电，USB 单路 START | QUERY/ACQUIRE 匹配；UVC 输出；Camera 仅首次启动；无 DMA 错误 | NOT_RUN |
| H02 | V1 完全断电，Storage 单路录像及抓拍 | 授权后挂载；JPEG/AVI 可解析；真实 SD 写入和同步完成 | NOT_RUN |
| H03 | 两路同冷启动，消息同时待处理 | CameraTask 每轮最多一条；同非零 Session；两 grant；CameraStart 一次 | NOT_RUN |
| H04 | 双路运行后 USB STOP | Return/ACK 完成；Storage 持续有帧；不提前停 Camera；RELEASE 未证实时保留资格 | NOT_RUN |
| H05 | 双路运行后 Storage STOP | Finish/Close/Unmount 各自实值；真实帧归还；USB 持续运行 | NOT_RUN |
| H06 | 双路均业务停止，再次请求 START | 记录真实 BUSY/安全阻塞；不能要求尚未实现的 C2.2C 资格闭环或强制清表 | NOT_RUN |
| H07 | 可控真实 UVC Abort 失败后恢复 | 原 ACK IO 不改写；后台归还完成后只提交一次 RELEASE；保留 grant 直到真实证明 | NOT_RUN |
| H08 | Camera START 部分失败、STOP 失败、Media ERROR 分别触发 | 保留真实错误；保护性停止有效；同轮不重复 BSP；无强制回收 | NOT_RUN |
| H09 | 队列高负载及连续 USB+SD 运行 | 通知合并不丢消息；真实栈水位、FPS、DMA/FMC 错误完整记录 | NOT_RUN |

失败判据：错误身份回复被采纳、未归还即释放、另一消费者被连带停止、伪造完成标志、原错误被成功覆盖或文件异常被计正常完成。任何一项出现应保存现场，不能通过延时、强制释放或清 Replay 获得通过。

C2.2C 仍需生产 released/idle 真实归还证明、完整 RELEASE 和 STOP_IF_IDLE/Shutdown 闭环；本轮不实现，H06 未获得完整关闭保证。
