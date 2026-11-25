# Q2 代码风格

结论 **NEEDS_FIX**。沿用项目 PascalCase 静态函数、模块前缀、文件内静态符号和 Allman 括号。Ethernet 公开 snake_case 已有调用者，保留。Service/TCP 与同域其他文件缩进不同。HTTP 解析、路由和响应组装的边界可跟踪，多个短 case 压在单行，超时和重试单位应集中说明。MAC Ring 和 Netif 有上限及 BUSY 路径，TCP 用 uint16_t 长度和固定容量约束复制，毫秒比较有回绕处理。公开 MJPEG 前置条件缺口见 ETH-Q04。多处生命周期错误用 err_t，硬件内部 EthResult_t 保留到快照。没有新增格式化规则。
