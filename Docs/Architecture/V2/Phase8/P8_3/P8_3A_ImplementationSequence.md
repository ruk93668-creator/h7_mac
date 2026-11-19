# P8.3-A 实施顺序建议

设计基线 e0ad3b5744b73095f09b3609738e7431956082c6。本轮停在只读审计。下列建议测试均为 NOT_RUN；Grant→实际 Lease 和稳定关闭均为 NOT_PROVEN。

| 顺序 | 建议工作 | 退出门禁和回退 |
|---|---|---|
| 0 | 固定源码与原判定；先审 A01–A09，保持 Legacy 路径与 P8.2 诊断端口 | 编译、Map、B2 ID23/31 与历史结果作为基线；批准前不得绑定真实 Backend |
| 1 | Camera owner 提供只读物理观察候选。Raw 池实例与独立 token，JPEG session/id 和真实 owner bits，SD-only 区分内部持有 | 验 Raw/JPEG、双 span、FIFO 复用与陈旧 M。失败回退只读，禁止增加物理引用计数 |
| 2 | 同一 CameraTask 串行绑定 G/H 和 G/R，覆盖 USB Request 与 StorageOffer 两个入口，并处理 SD-only | close-before-admit、admit-before-close 且无 M、已绑定可排空、旧请求/通知均可观测。尚不开放真实 RELEASE |
| 3 | USB 提供匹配 B/H/M 的终止；Storage 提供 S/M CPU 读完和独立 IDMA 状态；Media owner 回收 | 晚完成、错误 token、USB/SD Abort 失败、Media Return 失败；O0/O2 和实板故障注入。读者不明时保留实际 loan |
| 4 | 绑定真实回收收据并回传消费，使用原 MR_Retire 条件 | ACK 满、晚消费、重复发送及 S/B 重用；同一个 M 最多释放一次，缺确认继续 pending |
| 5 | L_verify 真核验，维持另一路；最后退出观察全部 loan、H/T、Media/DMA/STOP 首错 | 按矩阵实板核验单路、联合、SD-only 和故障；人工批准后再决定是否开放，不进入 P8.4 |

记录最小字段包括源码 SHA、owner、原 G/R/H/B/M/S/T、发生顺序、owner bits、DMA/端点或 Copy 结果、ACK 发送和实际消费。回退只能关闭新入口并排空既有 loan，不得走可能遗失持有的绕行路径。
