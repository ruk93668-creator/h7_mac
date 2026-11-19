# P8.1 资源预算报告

ARMCC5 完整 Rebuild，单位 B。V1 对照源码 a3cc70c，候选源码 bdd999b1e8e25e08988a208197ef692b9ae56ff7，本地 Map 和哈希见 output/P8_1/baseline/、output/P8_1/build_20261119_121242/ 和 output/P8_1/link_and_resources.json。

| Target | 指标 | V1 | P8.1 | 差量 |
| --- | --- | ---: | ---: | ---: |
| CM7 | Code | 139702 | 153134 | +13432 |
| CM7 | RO-data | 3114 | 5098 | +1984 |
| CM7 | RW-data | 1344 | 1344 | 0 |
| CM7 | ZI-data | 100992 | 100992 | 0 |
| CM4 | Code/RO/RW/ZI | 26350/1438/936/3576 | 26350/1438/936/3576 | 0 |

CM7 Flash 主执行区从 142620 增至 158036 B，容量 1048576 B。CM7 DTCM RW/ZI 区 69248/131072 B，空余 61824 B。SDMMC AXI 专用区 32768/32768 B，共享 SRAM 320/4096 B。三个 RAM 区均与 V1 相同，CM4 私有 RW/ZI 区也保持 4192/294912 B。SDRAM 七槽 Raw 固定池、JPEG FIFO 512 KiB 和两个 8 KiB 丢帧块由后端管理，未见项目或源码新增分配。Initcall/Device Export 大小不变。

实际 Map 中 Camera/USB/Storage/Control/IPC 静态栈分别为 6144/8192/8192/3072/2048 B，合计 27648 B，分配未增。未新增运行 Context 或静态队列。ARMCC5 测得调用方可选的 Composition Scratch 696 B、Candidate 276 B、V2Issue 20 B、GraphIssue 28 B，合计 1020 B。专项含夹具的 O0/O2 峰值分别为 1196/1180 B。没有真机任务调用该接口，真实任务栈高水位和 WCET 未测。

P8.1 静态镜像预算通过。后续运行态 Context、队列和栈需要重新核算。
