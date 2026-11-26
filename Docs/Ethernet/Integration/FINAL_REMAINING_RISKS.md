# 剩余风险和门禁

审查日期：2026-11-26（Asia/Shanghai）。源码冻结：`78580073610ff4cfe06a4b004b1b0d75bffef499`；main：`851c1115fe097f7fd2e0bdcaa5f7b3f8c03feb71`。

| ID | 等级 | 状态 | 关闭条件 |
|---|---|---|---|
| HW-MPU | P1 | NOT_RUN | 实读MPU/CCR和DMA停止位，不能用源码代替 |
| HW-JOINT | P1 | NOT_RUN | 100轮实板生命周期、60分钟多外设及资源回收 |
| HW-FIFO | P1 | OPEN，硬件待验 | 历史DCMI DMA FIFO Error根因尚未完全关闭；保留8字节复制及错误检测 |
| PERF-20 | P2 | OPEN | 20FPS没有已验收基线；50ms只是请求周期 |
| STACK | P2 | NOT_RUN | 静态预算完成，实际最大负载水位须N5保存 |
| IPC-LEGACY | P2 | FAIL / BASELINE_FAIL | 旧Status测试缺app_status.h，与main一致；原日志保留 |

NET-01至NET-06的正式实现及软件回归已经完成，旧检查点的未实现陈述只作历史。
这些硬件风险不阻止交付待验收包，但阻止宣称硬件PASS或READY_FOR_MERGE。
HTTP与E5非HTTP模式互斥是原双角色TCP范围，不承诺两个TCP服务器同时工作。
实板若有CRC、资源增长、DMA错误或FPS不足，保留FAIL并定位，不能改门槛、关缓存或错误检测。
