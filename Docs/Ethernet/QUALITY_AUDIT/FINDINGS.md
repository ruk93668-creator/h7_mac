# 审计问题

| ID | 等级 | 位置 | 问题 | 影响 | 修改建议 | 阻止合并 |
|---|---|---|---|---|---|---|
| ETH-Q01 | P2 | Platform/Ethernet/eth_udp.h:10 | UDP 回调借用期及公共 API 参数、错误和单位覆盖不一致 | 调用者可能保存临时数据指针，或误读统计量。 | 在 UDP、Netif、Transport、Network 头文件补齐关键参数、回调上下文和借用周期。 | 否 |
| ETH-Q02 | P2 | CM7/Service/Ethernet/eth_http.c:21 | 服务和 TCP 使用空格缩进，MAC 等使用 Tab；HTTP 状态机集中且有分散的策略常量 | 审阅和修改超时策略需要跨多处，容易降低可维护性。 | 以仓库同层成熟 C 文件为基准分批统一，仅命名有策略含义的常量。 | 否 |
| ETH-Q03 | P1 | Platform/Ethernet/eth_network.c:188；Platform/Ethernet/eth_transport.c:11；CM7/Service/Ethernet/eth_http.h:43 | 正式运行缺少唯一 Raw API 执行者及完整停止/重启契约。Network 和 Transport 保存全局所有者，API 没有完整解除绑定路径。 | 接入 FreeRTOS 后无法证明重复 Start、Stop、错误回调及任务退出时资源完整回收。当前默认关闭避免触发。 | 明确任务拥有者、Poll/定时器顺序、请求与停止完成条件，验证串行和故障路径后再开放运行。 | 阻止正式启用。本次最终审核也应明确批准仅链接范围。 |
| ETH-Q04 | P2 | CM7/Service/Ethernet/eth_mjpeg.c:16；CM7/Service/Ethernet/eth_mjpeg.c:78；CM7/Service/Ethernet/eth_mjpeg.h:90 | MJPEG 低层公共 start 要求先初始化对象及配置，且 poll 依赖有效帧源或非空静态数组、非零间隔。签名与注释没有完整声明这些前置条件。 | 现有 HTTP 调用通过入口校验。直接接入新调用者可能读取未初始化状态或在间隔/帧数无效时触发除零。 | 明确契约并验证无效配置，或缩小低层接口可见范围。 | 否，限现有调用范围 |
| ETH-Q05 | P1 | Tests/Ethernet/e7_camera_provider.c:12；Tests/Ethernet/e7_camera_provider.c:69；CM7/Service/Ethernet/eth_mjpeg.c:197 | 跨任务 Camera Frame 到 JPEG 私有缓冲的正式租约与停止契约尚未验证。历史 DMA FIFO Error 未关闭。 | 独立 E7 的归还事实无法外推到正式相机消费者及 DMA 停止失败场景。 | 独立验证所有权、停止失败、隔离和复用条件；维持现有 8 字节分段策略。 | 阻止正式视频启用。 |
| ETH-Q06 | P1 | MDK-ARM/h7_mac.uvprojx:1；MDK-ARM/h7_mac_CM7_custom.sct:35 | 正式接入及 Service 移动仅在本地工作树，远端 HEAD 不包含同一候选。 | 无法按远端 SHA 重复本次候选构建，审阅对象存在实质差异。 | 由人工决定候选清单和后续 Git 流程，排除 RTE 本机修改。 | 是 |
| ETH-Q07 | P1 | MDK-ARM/h7_mac_CM7_custom.sct:35；CM7/Core/Src/main.c:317 | 正式 CPU 区有 Scatter 预留，正式启动没有独立固件 Region 5 的硬件读回及并行实板回归。 | 链接通过只证明各对象地址和容量。正式网络/相机共存、Cache 和 IRQ 行为未确认。 | 明确正式启用时 CPU 区属性，按已知实板步骤确认 CCR/MPU/IRQ 以及 Camera、USB、SD。 | 阻止正式启用。 |
| ETH-Q08 | P3 | MDK-ARM/RTE/_h7_mac_CM7/RTE_Components.h:6 | 本地生成文件的工程名从正式项目变为 E7 V1。 | 当前差异仅注释，构建工具仍可能跨项目重写该文件。 | 保持本地差异，正式提交时显式排除；使用隔离构建。 | 否 |
| ETH-Q09 | P2 | CM7/Service/Ethernet/eth_http.c:240；CM7/Service/Ethernet/eth_http.h:53 | 测试用快速发送调节入口进入正式接口，并由可变全局状态控制。 | 默认关闭，后续调用者能绕过正式性能配置约束，API 边界不明确。 | 在生产配置与实验开关之间建立明确边界，并保留 E7 对照。 | 否，限开关默认关闭 |
| ETH-Q10 | P1 | MDK-ARM/h7_mac.uvprojx:1；.gitignore:1 | 远端 main 已前进，正式工程文件和忽略规则在双方均有改动。 | 本轮构建验证的是现有候选，不能代表最新 main 的结合结果。 | 人工核对 main 与候选的工程组、配置和忽略规则，确定集成版本后重跑 CM7、CM4、E4–E7。 | 是 |
| ETH-Q11 | P2 | Platform/Ethernet/eth_udp.c:119；Platform/Ethernet/eth_udp.h:21 | UDP 统计快照直接解引用输出指针，头文件未声明其必须非空 | 当前 Transport 提供有效地址。独立调用传空指针可能触发 HardFault，与同层部分统计接口处理方式不一致 | 声明非空输出参数或与同层统计接口统一处理，并覆盖边界用例 | 否，限当前调用者 |
