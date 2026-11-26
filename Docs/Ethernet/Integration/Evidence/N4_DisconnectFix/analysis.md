# 浏览器断开修正
50ms 请求周期下，RST 后可能遗留下一帧 WAIT_OFFER。原断言要求120ms内退休，修复前119场景O0/O2失败。
NetworkTask 现在查询 HTTP 的实际流连接状态，并撤销已离开客户端的需求。
撤销不清空租约，不强制释放私有 Buffer；迟到 OFFER 仍通过 FRAME_RETURN/RETURN_ACK/CONSUMED 合法归还。
实际 HTTP Source 的 release 负责私有帧归还，重连后允许新需求；两次真实 TCP握手/RST/借帧均验证。
没有扩大等待窗口，没有改为忽略错误。
新 START 也按 Camera 的既有契约在资源安全退出后建立新事务首错快照，驱动/内存累计错误和旧终结消息保留。
最终预固化生产测试170/170通过；最终全部构建矩阵仍须基于本提交重跑，实板NOT_RUN。
