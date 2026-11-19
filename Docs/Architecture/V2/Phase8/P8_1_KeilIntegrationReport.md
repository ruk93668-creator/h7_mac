# P8.1 Keil 集成报告

| Target | 实际 ARMCC5 Rebuild | V2 编译 | V2 链接 |
| --- | --- | --- | --- |
| h7_mac_CM7 | 0 Error、0 Warning | 原 27 个 C 全部编译，另有 v2_board_v1.c | Map 加载 16 个 V2 对象的代码或数据，其余 12 个对象完全裁剪 |
| h7_mac_CM4 | 0 Error、0 Warning | V2 0 个 | V2 0 个 |

两个 Target 使用 ARMCC 5.06 update 7 build 960、原 C99、优化和告警设置。包含路径、MPU、scatter、现有宏和 .uvoptx 保留。原始工程哈希、字节快照及 Git Diff 在本地 output/P8_1/baseline/。提交中的工程基于原 Git 版本精确插入登记，工作区基于原用户文件插入同样内容。两个版本的活动 C/汇编路径集合和 Target 的 Cads、Aads、LDads 设置一致。CM4 XML 分别与对应原文件逐字节一致。

首次在 CM7 新建独立分组时，实际 CM4 Rebuild 仍枚举该组并失败，原失败留在 output/P8_1/build_20261119_120524/。最终逐项登记到既有 CM7 Native Runtime 组，该组在 CM4 已显式禁用。最终 CM4 日志及 Map 未出现 V2 对象。

CM7 加载 V2BoardV1_Compose、V2BoardV1_Catalog、V2Resolver_Resolve、V2Composition_Validate 和 Graph_ValidateSet，未出现未解析或重复符号。实际加载区包含 v2_board_v1、v2_registry、v2_composition、media_format、media_graph、media_source、managed_lifecycle、camera_format、camera_module、storage_consumer、storage_format、storage_module、usb_consumer、usb_format、usb_frame、usb_module。三个 Endpoint、四个 Control 源、两个 Supervisor 源、media_resource、storage_backend、storage_frame 完全裁剪。两核 Initcall/Device Export 的对象和字节与原固件一致。

验证源码 bdd999b1e8e25e08988a208197ef692b9ae56ff7，构建日志及产物在本地 output/P8_1/build_20261119_121242/，机器核对结果在 output/P8_1/link_and_resources.json。

| 产物 | SHA256 |
| --- | --- |
| CM7 AXF | fdb125dffdc5bb132c6f3d8f5f99a96a0e25e34a6cb61eb31ccc169b08c0cb5d |
| CM7 MAP | 620411a914f25f5f6c6dd91f50ee5fa11c4d60b91ef96b64ac960549c44a9219 |
| CM4 AXF | 9cab16176736e9cdfcf325b99d631f3dba7147ce5bb6b247836f7e9e15d8e696 |
| CM4 MAP | 9cf375de736dea8480f088d2923d4f282025feca69ec156f50bb2afdabd28d7d |
