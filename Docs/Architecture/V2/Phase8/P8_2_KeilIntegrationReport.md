# P8.2 Keil 与链接报告

两目标 ARMCC 5.06 update 7、原编译选项。仅在现有且 CM4 禁用的 CM7 Native Runtime 分组新增 v2_rtos_runtime 和 v2_rtos_transport 的 C/H 项。没有新增 Include Path，也没有改变工具链、scatter 或 .uvoptx。用户原配置和差异记录于 output/P8_2/original.uvprojx、original.uvoptx.sha256 与 keil_preexisting.diff，反向去掉四条登记后和原文件逐字节相等。

CM7/CM4 完整 Rebuild 各 0 Error、0 Warning。CM7 Map 加载真实的 V2Runtime_Round、V2Rtos_Init、三组 Endpoint、CS_Process、CSExec_Process 和 EM_Process。CM4 没有 V2 对象，CM4 HEX 与 P8.1 逐字节相同。两核 Initcall 与 Device Export 对照通过。隔离重编和链接脚本原始输出在 output/P8_2/build_20261119_133806/，结构化核对见 output/P8_2/map_audit.json。构建启动时 HEAD 为 P8.1，快照包含工作区候选，提交前逐字节验证对应最终实现 182dbb0ba4c06048e312077cbe57174e6e29baf4。

| Target | 产物 | SHA256 |
| --- | --- | --- |
| h7_mac_CM7 | .axf | bac504241d3f238e41b602658317f2ec897dd567dfdedb3d57c307f78f98a6ac |
| h7_mac_CM7 | .map | 754639e7a7dea5db0e1b88fac63a6ada768b3752c4f162b4706bf80da4bfc5aa |
| h7_mac_CM4 | .axf | 00b0ec8ab6e1fa0daf4e07ba2174ce361f4ddf08159a172bb3665d2aa99c9ab9 |
| h7_mac_CM4 | .hex | 7be9685cb70201486281dcc0a327ea485705f3c7ba271a5dc2e1feb7ccdbd301 |
| h7_mac_CM4 | .map | 9cf375de736dea8480f088d2923d4f282025feca69ec156f50bb2afdabd28d7d |
