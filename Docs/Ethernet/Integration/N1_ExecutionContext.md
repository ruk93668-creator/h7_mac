# 唯一网络执行上下文审查

状态 SOFTWARE_BLOCKED。尚未新增 Network Task，现有正式任务清单中没有 Network。当前协议仍只能在独立测试循环运行。

证据为 Common/Inc/runtime_event.h:9–14 的五个任务身份，CM7/ApplicationV3/Core/app_services.c:27–91 的三个服务清单，以及 Runtime/service_record.c:85–94 按该清单分配发布信号的规则。任务创建应继续使用 AppV3_CreateThread 和 AppV3_ServiceRun，完成 Record 初始化与启动报告后再运行网络。

需要新增独立 Network 身份和静态 Service 记录，并同步审查 App_Events 的所有目标启动屏障、任务计数、Record 信号槽与业务结果槽。既有名字查找只提供身份，不能作为 lwIP 写权限。Network 所有者校验应同时检查任务与 ISR 上下文。其他任务只提交邮箱请求，诊断由值类型快照读取。

建议执行顺序为 Network Poll、Transport Poll、HTTP Poll、生命周期收尾、Mailbox Flush，然后有界阻塞。独立网络 RX budget 已存在。IRQ 只保留原硬件事件处理，唤醒属于提示。所有 timer 和 Raw API 调用仍由同一 Network Task 执行。

本轮没有实现上述身份、所有者门禁和任务，所以合法/非法所有者、多任务请求、定时器、积压与任务公平性专项均为 NOT_RUN。
