# P1-D：UVC ISO生命周期审计与未完成项

VERIFIED（起点未变）：usb_uvc_class.c:366 Task Process在取消时调用PayloadCancel，BUSY后同步Abort81，成功即ReleaseFrame；Alt0:1003同步Close81，确认成功后才清Alt并ReleaseFrame:1105。DeInit:512依赖Alt0。DataIn:1063只记录当前packet.token/in_sequence；SOF:1078只发布最新额度。Payload_AbortPacket清软件引用不能替代USB硬件证明。

VERIFIED：app_uvc_bridge.c:267的USB侧Return/ACK流程与:456的Camera释放仍归原所有者；本轮没有修改Camera、Media、Payload格式、UVC描述符、Probe/Commit或视频能力。

UNRESOLVED / NOT_IMPLEMENTED：本轮没有实现新的ISO IN停止原语或迁移UVC类生命周期。现有Bulk IN proof只支持Bulk类型；不得将Bulk主机ACK/NAK假设套给ISO。ISO需要其独立Transfer、Incomplete/XFRC/EPDISD预清事实、PIO和真实定向FIFO Flush结果、原Frame全部消费者引用及旧事件退休契约。现有HAL IN EPDISD路径仍自行Flush并丢弃底层返回，仅看到HAL_OK不足以建立该票据。

INFERRED：Task请求关闭新Payload和SOF发送资格；BSP基于ISO实际Disable/事件/FIFO条件形成proof；Task确认Payload/Bridge只归还该Frame lease一次；最后才提交Alt0/DeInit。Alt0同步控制请求和Stop/Reset/Disconnect的统一提交需P1-E门控配合。这个目标顺序是设计，不是当前已运行实现。

BLOCKED：全设备及Alt请求联合闭环被EP0门禁阻断；独立ISO停止证明亦未闭合，不以旧同步Close替代新证明。本轮到关键门禁集中交付，不把P1-D或默认UVC硬件回归标成完成。已有UVC生命周期历史FAIL原样保留，Payload O0/O2 case37两项FAIL继续保留。NOT_TESTED：所有真实视频、SOF抖动、活跃ISO Stop、Frame迟到事件板测。
