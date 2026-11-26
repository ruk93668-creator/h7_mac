# 复审修正
STOP 移除 netif 后仍需由 NetworkTask 推进全局 cyclic timer。117 号场景修复前失败，新增独立 timers poll 后 O0/O2 通过。
126 号新增测试原先把原生队满返回值误写为 NO_RESOURCE，并把待完成请求时的 EventLoop BUSY 误写为 OK。
源码 queue_pool.c 的零等待满队列明确定义为 BUSY，已按原契约逐项验证 BUSY/完成顺序，未修改生产 Mailbox 或放宽为多值断言。
验收版首次编译失败因测试生产者未包含 Tasks/supervisor.h 的正确路径；改为显式仓库内相对头文件路径，最终完整矩阵待复验。
生产配置改用 ETH_NETWORK_SERVICE 选择正式 lwIP 功能，独立验证保留原兼容配置入口。
原主线 IPC Status 缺少 app_status.h 的失败日志保留，不改旧测试或 IPC 实现。
最终协议负载与内存验收以最终冻结提交矩阵为准。
