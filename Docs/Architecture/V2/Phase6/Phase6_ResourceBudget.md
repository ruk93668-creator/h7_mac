# Phase 6 资源预算

ARMCC5 Cortex-M7 O0/O2 下使用真实 `sizeof`、`offsetof` 和对象 ELF 分配节。模块不额外实例化帧池或 Media Resource 物理账本。

| 对象 | B |
| --- | ---: |
| VStorage | 856 |
| VStorageFS（含 256 B 读取校验工作区） | 320 |
| VStorageClient | 88 |
| VStorageEndpoint | 124 |
| 以上四项合计 | 1388 |
| 内嵌 VStorageFrame | 260（已计入） |
| VStorage.consumer / frame 偏移 | 276 / 572 |
| AppMessage / LifecycleMessage | 52 / 56 |

六个独立 Storage 编译单元合计 O0 RO 8154 B，O2 RO 6780 B，模块级 RW/ZI 为 0。对象大小不含生产队列、既有 MR/AVI 索引及 FatFS 缓冲；原 AVI 索引仍由既有服务使用 65536 B SDRAM。现阶段不新增生产队列，也未把独立模块登记到 Keil，因此正式固件净增量待集成 Map 核算。

ARM 指令模拟的选定联合调用链最低 SP 差值为 O0 1152 B、O2 1112 B，包含测试夹具、Composition 和间接回调。真实 StorageTask 的当前配置为 2048 words / 8192 B，未调整。真机 FreeRTOS 任务高水位、SDMMC 等待、连续写入最坏执行时间待测，独立模拟不能据此承诺最坏响应时间。

| 原业务 Target | Code | RO-data | RW | ZI |
| --- | ---: | ---: | ---: | ---: |
| CM7 | 137690 | 3114 | 1340 | 100932 |
| CM4 | 26350 | 1438 | 936 | 3576 |

两核 Keil 完整 Rebuild 均退出 0，0 Error、0 Warning。保存原工程哈希、AXF/Map/HTM 及生成的 HEX 哈希，原有输出和工程改动原样保留。独立模块 Map 和命令见 `output/V2Phase6/joint`。双核构建日志及清单见 Git 中 `Tests/V2Phase6/Evidence`。
