# Repository Cleanup Keil 报告

ARMCC5 使用同一套用户 Keil 配置的隔离副本构建。原始 .uvprojx 的 SHA256 为 `6a866dc2ea07abb459f760fc471312dc1f5a7b4cbbb63f921730ce441700a689`，.uvoptx 为 `765e9e89ce48a74247281b7c3f9630203a3acc80c25ebf6b75569af4c7c70269`。两份外部配置保持原样且未提交。

- CM7 完整 Rebuild 为 0 Error、0 Warning。MAP 中查不到归档的三条队列及 `App_LifecycleMessagesInit`，V2 的轮次入口和三个 Endpoint 仍在产品中。产品共享 Lifecycle、Replay、Generation 仍留在工程中。
- CM4 完整 Rebuild 为 0 Error、0 Warning。原 Target 生成 HEX，两次构建 SHA256 均为 `7be9685cb70201486281dcc0a327ea485705f3c7ba271a5dc2e1feb7ccdbd301`。
- CM7 原 Target 未开启 HEX。证据使用真实 MAP、AXF 及构建日志。
- 保留编译器、Target、任务配置、链接脚本及用户 Include Path。归档目录仅由独立历史夹具读取。

构建证据目录为 output/RepositoryCleanup/build_20261119_145222，results.json 绑定实施提交 519de5267cafefe3b5b28e9f9160002c57a7520c。
