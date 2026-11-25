# Network 生命周期依赖审查

状态 SOFTWARE_BLOCKED。尚无正式 Network Service，下面是对现有实现的缺口记录。

- Platform/Ethernet/eth_network.c:17–18 保存 lwIP 初始化事实及唯一 Network 指针，初始化后没有停止、netif_remove 和所有者解除接口。
- Platform/Ethernet/eth_transport.c:11–31 的 init 设置静态绑定，重复调用返回 ERR_USE。Link 变化只停止端点，不能解除 Transport 配置。
- CM7/Service/Ethernet/eth_http.c 的初始化、源配置和连接状态同样为单例，缺少外部停止与完成查询入口。
- eth_tcp_stop 会停止 listener 和已跟踪连接。TIME_WAIT 由 lwIP 自身持有，句柄关闭不能单独证明这些 PCB 已退休。

正式服务应复用 AppV3HardwareRequest_t、AppV3Mailbox_Request/CompleteHardware 和 APP_V3_HW_* 状态。Record 发布表示服务实例可接受命令，网络地址、链路、listener 和视频状态必须分别查询。START 可在实例及硬件检查完成后报告服务启动，未接网线不能被误报为有效 IP。具体启动终结仍需生产实现与用例证明。

STOP 应先关闭 HTTP 和新帧准入，保留归还事务至 Camera 真实 RETURN_ACK，再推进有序 TCP 关闭；超时的连接按原 API 终止，继续驱动 lwIP 定时器直到所属 PCB、pbuf 与发送资源退休。随后撤销 DHCP、ARP 与接口，再调用 MAC Stop 并检查硬件结果。失败保留旧状态和原身份，禁止 memset 或清除 BUSY 后重新启动。

这些机制尚未实现，START/STOP/RESTART、部分失败、旧事件与资源耗尽专项均为 NOT_RUN。
