# Q8 完整编译与回归

结论 **PASS**（本轮工作树快照的全部目标和主机回归）。默认 Git whitespace 检查另有发现，见下。构建使用 Keil UV4 全量重编译，主机使用 MSVC /WX；本轮原始日志、MAP 执行区摘要及校验 JSON 见 BuildEvidence。Flash/RAM 两列为编译统计口径，实际装载大小、区域对齐、压缩及 UNINIT 以 MAP 为准。

| 目标 | 结果 | Error / Warning | Code / RO / RW / ZI (B) | Flash / 静态 RAM (B) | 地址 |
| --- | --- | --- | --- | --- | --- |
| 正式 CM7 | PASS | 0 / 0 | 200778 / 4894 / 1636 / 156716 | 207308 / 158352 | [MAP 摘录](BuildEvidence/h7_mac_CM7-map-regions.txt) |
| 正式 CM4 | PASS | 0 / 0 | 26302 / 1438 / 936 / 3576 | 28676 / 4512 | [MAP 摘录](BuildEvidence/h7_mac_CM4-map-regions.txt) |
| e4 | PASS | 0 / 0 | 26842 / 1110 / 156 / 38388 | 28108 / 38544 | [MAP 摘录](BuildEvidence/eth_e4-map-regions.txt) |
| e42 | PASS | 0 / 0 | 38030 / 1242 / 212 / 38720 | 39484 / 38932 | [MAP 摘录](BuildEvidence/eth_e42-map-regions.txt) |
| e5 | PASS | 0 / 0 | 67546 / 1614 / 348 / 68588 | 69508 / 68936 | [MAP 摘录](BuildEvidence/eth_e5-map-regions.txt) |
| e6 | PASS | 0 / 0 | 70262 / 51434 / 332 / 64380 | 122028 / 64712 | [MAP 摘录](BuildEvidence/eth_e6-map-regions.txt) |
| e7 | PASS | 0 / 0 | 109938 / 4154 / 516 / 127968 | 114608 / 128484 | [MAP 摘录](BuildEvidence/eth_e7-map-regions.txt) |
| e7_v1 | PASS | 0 / 0 | 111966 / 4154 / 520 / 128052 | 116640 / 128572 | [MAP 摘录](BuildEvidence/eth_e7_v1-map-regions.txt) |
| e7_v1_axi | PASS | 0 / 0 | 112010 / 4154 / 520 / 128052 | 116684 / 128572 | [MAP 摘录](BuildEvidence/eth_e7_v1_axi-map-regions.txt) |
| e7_v1_qvga | PASS | 0 / 0 | 112014 / 4154 / 520 / 128052 | 116688 / 128572 | [MAP 摘录](BuildEvidence/eth_e7_v1_qvga-map-regions.txt) |
| e35 | PASS | 0 / 0 | 68398 / 2862 / 37096 / 81824 | 108356 / 118920 | [MAP 摘录](BuildEvidence/eth_e35-map-regions.txt) |

正式 CM7 实际 MAP 装载 207308 B。Ethernet CPU 窗口 0x24010000 使用 43232 B，余 22304 B；Ethernet DMA 在 0x24078000 占 12544 B；DTCM 在 0x20000000 占 69488 B；SDMMC 在 0x24000000 占 32768 B；IPC 在 0x38000000 占 320 B。正式 MAP 的 ETH_IRQHandler、HAL_Init、Device_RegisterAll 各有一个最终代码符号。Initcall 区 192 B，Device Export 区 4 B。未发现测试入口、E7 provider 或 E6 样片进入正式镜像。独立目标均通过其 DMA MAP 地址检查；E4–E7 通过 CPU 窗口检查，E7 的 JPEG 私有区按对应 V0/V1 SDRAM 或 AXI 配置检查。

主机各用例独立进程返回 0，覆盖 E4.1 ARP/ICMP/错误路径，E4.2 DHCP 租约与资源压力，E5 三通道和 TCP 关闭/重传，E6 HTTP/MJPEG 帧完整性与虚拟 605 s，E7 动态 JPEG 租约和 Stop 隔离，另跑了 E7 QVGA 变体。605 s 是虚拟时钟，历史实板成绩另见 E7 证据。正式候选实板与最新 main 集成构建均为 NOT_VERIFIED。

首轮独立构建的隔离目录没有 Git 元数据，生成器在读取 HEAD 后处理阶段中止。复跑将这条只读查询指向原工作区，完整构建和断言通过。原工作区的构建脚本未改。

默认 Git diff --check 对既有 CRLF 报警。仅在单次命令中启用 cr-at-eol 后，已提交的项目改动通过，当前工作区只剩 RTE 第 6 行既有尾随空格。检查日志见 static-checks.json 和各 whitespace 文本。保留 RTE 原样。
