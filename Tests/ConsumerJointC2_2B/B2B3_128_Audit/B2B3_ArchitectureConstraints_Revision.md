# Architecture Constraints Revision

基线9cf537d47c1673b05ee20dd8a0c6f5e97965b660；用户本轮要求为正式设计约束。本文件冻结目标边界，不声称现有实现已满足。当前源码仅两个固定消费者，协议/资格/Media尚未泛化。本轮不修改Camera API或生产代码，不新增消费者。

## AC-01 Camera Consumer Verification

Camera核验必须以明确消费者身份为入口，未来不能不断增加USB/Storage/Network专用Camera核心函数。采用消费者身份到权威交接证据提供方的可扩展契约方向，具体注册/函数表/容量设计待审，不提前实现通用框架。现有s_qualification_ops和两个固定entry不是已实现泛化机制。

消费者自己的业务模块/资源层可按职责实现适配，Camera核心负责受控身份和安全条件；不得以固定if USB/else Storage无限堆叠，不新增第二套全局Frame引用计数。当前两路分别核验，不用global HasLoan替代。此前示例中消费者专用Camera核验函数仅为历史方向草案，后续API设计必须遵守本身份化入口约束。

## AC-02 ACQUIRE Identity

完整grant身份至少包含session_id/ACQUIRE request_id/consumer_id/consumer_generation，并保留来源与受控请求操作。它证明获得哪一代资格；不是Frame Lease身份。创建与存储依据consumer Begin/QUERY/ACQUIRE及Qualification grant_*，不得用RELEASE request_id取代ACQUIRE编号。

## AC-03 RELEASE Identity

RELEASE拥有独立request_id，关联原grant并服从当前Rx窗口/退休高水位及generation规则。迟到旧RELEASE不能撤销新grant；两身份编号不等同。现有released回调参数是grant身份；未来适配必须明确谁核对RELEASE窗口，不能把示例RELEASE消息直接当grant参数。不得为此更改Replay容量、接替或引入配额。

## AC-04 Media Ownership Extensibility

未来Media所有权须能表达Network真实持帧；当前usb_owned/sd_owned仍独立，不能让一个消费者退出释放另一方引用。未来Network没有实际持有Frame时不能凭控制连接、发送状态或网络上线就声称参与该帧资源账本；Control Plane活动不等于Frame owner。

资格申请与实际持帧必须继续分离：未来数据面可在合法接收Frame前申请采集资格，但不能把ACQUIRE成功当作已持Frame；无实际帧持有时不创建虚假Frame Lease。Network活动资格的按需申请/释放策略须另审，不能据“仅持帧参与”要求未授权先拿Frame再ACQUIRE。

当前FIFO硬编码usb_owned/sd_owned，Qualification固定USB/Storage，LifecycleValidate绑定身份域；不能声称支持第三消费者。未来需单独审Media适配、协议身份域和资源预算；本轮不改其算法/ABI，不注册Network。

## AC-05 ACK Semantics

必须区分传输完成、DMA不再访问源Frame、ACK入队、ACK被所有者匹配消费、Camera实际Release确认、消费者退出确认。它们不互相替代。现有Return ACK只有真实StreamReleaseFrame成功后构造；生命周期释放回复只有真实资格核验后才能成功。业务ACK另属业务结果。

未来TCP接受数据、网络栈接受指针、Ethernet发送排队、Wi-Fi驱动接受数据或远端收包均不是Camera Release ACK。优先复用现有任务消息、ACK队列协作思想；不是不经审核把新网络消息直接塞进现有两路协议。不得新增平行可靠协议来绕过当前Tx/Rx/Replay边界。

## AC-06 DMA Ownership

原始Frame归还前，必须证明所有仍可能访问它的CPU/异步DMA操作都终止访问。发送函数成功不构成此证明。

复制式：只有对原Frame的所有复制读取完成，异步复制DMA完成/Abort已确认且无其他引用，后续发送仅访问独立缓冲时，才可交还原Frame；独立发送缓冲仍需自己生命周期，网络发送成功不代替复制完成。

零拷贝：所有指向原Frame的网络描述符/重试队列/分包操作及DMA引用都解除且完成或确认Abort后，才可Return；多端口/多次异步操作须逐一确认，不能靠任一操作完成推导全部完成。失败/断线不得超时强回收；未知保持所有权。未来可有传输局部在途记录，不得复制Media全局引用账本。

## 未来Ethernet/Wi-Fi扩展冻结

两类网络都需Control Plane：远程控制、参数配置、网络配置、状态/故障上报；Data Plane：实时视频、分包、异步发送、背压/丢帧、断线恢复。控制面独立于Frame Lease，不因控制连接存在就申请虚假帧持有。丢帧若已持源必须真实完成归还；断线不能绕过DMA终止证明。

这里只冻结需求，不选择TCP/UDP/RTP等格式，不实现驱动、Network Consumer、Transport Service、注册表或队列。不改变当前Camera API、现有USB/Storage资源独立性。

## 本轮合规

AC-01至06完整纳入。是否实现逐项另审；Grant→Lease与稳定关闭仍NOT_PROVEN。M4 //等OLED稳定 和HAL_Delay(500);保持，所有生产文件零改动。
