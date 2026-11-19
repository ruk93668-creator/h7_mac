# P8.1 静态装配报告

v2_board_v1.c 中 Camera 提供 Capture v1，USB 提供 UVC v1，Storage 提供 Recording v1。两消费者都需要 Capture v1。描述表内 Camera 位于两消费者之后，Resolver 仍计算出 Camera 先行的依赖顺序。三个描述符指向原 Lifecycle Ops，静态验证不会调用它们。

Graph 描述 OV5640 → CameraCapture → Raw 或 JPEG Backend。两个 Backend 互斥。Raw 只有一个 USB 出边，JPEG 可分别交给 USB 与 Storage，对应原 FIFO 的 USB/SD 两 owner。能力参考现有 FIFO loan、Storage 槽位及原 Storage 帧上限。40,000 B 是当前 V1 Profile 为 JPEG 预留的预算。纯格式验证调用现有 OV5640、CameraCapture、UVC FS 及 V2 Storage 格式函数。

| Profile | 格式 | 采集目标 fps | UVC 目标 fps | Storage 目标 fps |
| --- | --- | ---: | ---: | ---: |
| Raw USB | YUY2 160×120 | 10 | 10 | — |
| JPEG USB | JPEG 320×240 | 25 | 20 | — |
| JPEG Storage | JPEG 320×240 | 25 | — | 5 |
| JPEG USB + Storage | JPEG 320×240 | 25 | 20 | 5 |

速率均为配置目标。纯资源验证拒绝高于采集目标的 sink。四个 Profile 复用 CSProfile、V2Desired、Resolver、Graph 和 Composition。现有验证器保留否决权。调用方独占 scratch，成功只生成候选，失败保留输出，既有 Applied 快照在成功与失败时都未被改写。常量描述表和循环有界，无动态分配或硬件操作。

源码 bdd999b 的专项证据为 output/P8_1/static_20261119_121305_598735/results.json。真实 Grant→Lease 与稳定准入关闭仍待证明。
