# ID17 V1 实板验收准备

全部项目 **NOT_RUN**：本轮只执行软件/仿真与正式构建，未刷板、未连接硬件执行。本表不复用旧V1“七项通过”作为本轮独立证据。

候选源版本：8a83591b4baa0a850ccdfb269b68d752af5bee81。CM7 AXF SHA256：`ebc3d2a8c8760498de94d880631938511ad724ad91daf45737fd93a3dc4d7cfa`。CM4 HEX SHA256：`3030484c03d4983a192be39142dce7b6dfdb8e6c93a49e1f8285ee846540fb17`。实际烧录文件必须另逐项记录SHA256；若配置/入口变化，应记录新的固件SHA，不能沿用本候选哈希。

所有START/STOP只能从现有合法业务入口投递给所属Task；不得手改Session/Generation/grant、清Replay、直接Release帧或调用Camera硬件停止。当前released/idle仍NULL，双路业务停止后资格等待不等于真正关闭。

| 项目 | 前置及操作顺序 | 预期状态/观察位置 | 结果 | 实际固件SHA/原始记录 |
|---|---|---|---|---|
| USB单路冷启动 | 全断电；合法入口只启动USB；Storage不发START | QUERY→ACQUIRE→GRANTED；USB资格ACTIVE；Camera启动一次；Storage INACTIVE | NOT_RUN | 待填 |
| Storage单路冷启动 | 全断电；使用现有受支持配置禁止USB自动申请；仅启动Storage录像或抓拍 | Storage GRANTED后才Mount/创建文件；USB INACTIVE；Camera启动一次 | NOT_RUN | 待填 |
| USB+Storage同时冷启动 | 全断电；合法途径投递两路START；证明Camera首次处理前两条QUERY已同时待处理 | 不能把顺序START冒充并发；按真实CameraTask逐轮接收 | NOT_RUN | 待填 |
| 双路同时取得资格 | 延续并发冷启动，记录完整身份和匹配回复 | 两路Session相同非零，Consumer ID分别1/2；generation独立；两路ACTIVE/GRANTED | NOT_RUN | 待填 |
| Camera只启动一次 | 对上述一次冷启动周期的Device/Media启动入口计数 | Device_Start和CameraCapture_StartStream各1次；第二ACQUIRE复用资源 | NOT_RUN | 待填 |
| USB独立STOP，Storage继续 | 双路运行；USB STOP→Cancel/Return→Camera归还→Return ACK→Mode Stop→RELEASE | 原USB业务停止；Storage继续录制；grant在真实回复前保留；Camera不得提前停 | NOT_RUN | 待填 |
| Storage独立STOP，USB继续 | 双路运行；Storage STOP→文件收尾/帧归还→RELEASE | AVI/Close结果与帧归还分开记录；USB持续输出；Camera继续运行 | NOT_RUN | 待填 |
| 双路业务全部停止 | 分别合法STOP；核对所有在途帧和业务完成情况 | 业务停止与资格释放分开；released/idle缺失时安全等待，不能伪造已关闭 | NOT_RUN | 待填 |
| Abort失败后台恢复 | 合法可控故障注入；STOP首次Cancel=IO后解除故障 | 原ACK/FAULT IO保留；帧归还后Mode Stop成功；RELEASE只Stage一次，grant仍保留 | NOT_RUN | 待填 |
| 通知与消息队列 | 记录每轮接收、RuntimeEvent_Post调用和队列内容；另测满队列/通知合并 | 每轮至多1条，4条QUERY/ACQUIRE对应4次自唤醒提交；通知合并不丢队列消息 | NOT_RUN | 待填 |

每项补充：操作者/日期、PCB编号、CM7/CM4实际固件SHA、产品配置、START/STOP顺序、每条消息Source/Target/Operation/Request ID/Session/Consumer/Generation、资格前后状态、原始ACK结果、帧Lease/Return ACK及Device/Media启动计数。

物理观察可使用现有状态投影、合法只读Getter对应诊断、低频启动断点命中计数及不改变运行时序的跟踪。不得通过新测试专用资格对象替换真实App_USB/App_Storage。无法证明并发前置条件或缺少安全故障注入入口时应记录BLOCKED并说明，不把未执行项目写成PASS。

尚未实现的C2.2C验收单列：真实分消费者归还证明、RELEASE成功撤销资格、STOP_IF_IDLE硬件关闭、Shutdown/Restart闭环；均为NOT_RUN。当前两路退出停在安全等待时，不能强制清grant或强停Camera来获得“完成”。
