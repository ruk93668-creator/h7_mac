# N4 — 最终软件联合验收

审查日期：2026-11-26（Asia/Shanghai）。源码冻结：`78580073610ff4cfe06a4b004b1b0d75bffef499`；main：`851c1115fe097f7fd2e0bdcaa5f7b3f8c03feb71`。

**PASS（本轮软件门禁）**；旧IPC Status保留FAIL/BASELINE_FAIL，不记PASS。
最终源码从无未提交修改的独立副本构建，所有结果关联同一源码提交和文件SHA。
ARMCC5.06 update7 build960；MSVC；Cortex测试使用真实生产Service、FreeRTOS Queue/List和上游lwIP。
只模拟硬件/调度身份/网络对端，不能作为实板证据。

| 目标 | 结果 | Error / Warning |
|---|---|---|
| cm7_jpeg | PASS | 0 / 0 |
| cm7_raw | PASS | 0 / 0 |
| cm4 | PASS | 0 / 0 |
| firmware_e35 | PASS | 0 / 0 |
| firmware_e4 | PASS | 0 / 0 |
| firmware_e42 | PASS | 0 / 0 |
| firmware_e5 | PASS | 0 / 0 |
| firmware_e6 | PASS | 0 / 0 |
| firmware_e7 | PASS | 0 / 0 |
| firmware_e7_v1 | PASS | 0 / 0 |
| firmware_e7_v1_axi | PASS | 0 / 0 |
| firmware_e7_v1_qvga | PASS | 0 / 0 |
| production_network | PASS | 0 / 0 |
| hardware_acceptance | PASS | 0 / 0 |

| 回归 | 结果 |
|---|---|
| e41 | PASS |
| e42 | PASS |
| e5 | PASS |
| e6 | PASS |
| e7 | PASS |
| e7_qvga | PASS |
| application_v3 | PASS |
| media_lease | PASS |
| frame_dispatcher | PASS |
| storage_fs | PASS |
| uvc_reader | PASS |
| camera_i2c_O0 | PASS |
| camera_i2c_O2 | PASS |
| oled | PASS |
| ipc_v2 | PASS |
| ipc_status_legacy | FAIL / BASELINE_FAIL |
| phy_dma_mac | PASS |

ApplicationV3 O0/O2合计170/170，覆盖唯一拥有者/ISR、START/STOP/RESTART、停止失败重试、
真实DORA/ACD/T1、Client连接中停止、UDP耗尽恢复、队满BUSY及完成顺序、bound PCB阻止提前停止、
停止态timer、Camera队满/取消/旧epoch/归还失败、真实RST及重新握手再次借帧、四服务组合、
100轮软件回收、验收生产者无DHCP不得计数及失败后保留证据清理。

旧IPC Status在main与候选均因status_test.c缺app_status.h失败；原日志保留。
当前IPC V2和OLED通过，相关生产代码未改。没有修改旧测试或主线IPC来消除失败。
旧168/170记录和119场景修复前后数据均保留，不扩大120ms等待或关闭错误检测。
实际MPU/CCR、DMA停止、100轮实板和60分钟并行仍NOT_RUN。
