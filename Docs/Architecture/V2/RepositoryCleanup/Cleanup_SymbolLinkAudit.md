# Cleanup 1.4 符号与镜像审计

从基线和候选两套实际 Keil 工程重编。使用 Map 交叉引用与 AXF ELF 符号表，检查归档核心及 Camera 适配的 28 个符号。

| 构建 | 旧资格符号 | 代次函数 | CM4 单元登记 |
|---|---|---|---|
| b / CM7 | App_CameraConsumeStopResult, App_CameraQualificationStart, App_CameraReadStopResult, App_CameraRequestStop, App_CameraResourceState, App_CameraStopIdentityMatches, s_qualification_ops | 0x08001e5d / 40 B | 有 |
| c / CM7 | 无 | 0x08001c5d / 40 B | 无 |
| b / CM4 | 无 | 无 | 有 |
| c / CM4 | 无 | 无 | 无 |

CM7 Map 中 USB 与 Storage 消费者仍引用 App_QualificationNextGeneration。函数体前后逐字节相等。清理前的 CM7 核心处理入口已被链接器排除，AXF 仍含表中列出的适配符号。清理后这些适配符号也已移除，产品编译单元只保留共享函数。CM4 删除的是用户工程原有的额外登记。仓库目标此前已仅登记 CM7。
原 52B AppMessage、56B Lifecycle Message 的 ARMCC5 O0/O2 探针与 Replay 通过。两个 CM4 HEX 完全相同，SHA256 7be9685cb70201486281dcc0a327ea485705f3c7ba271a5dc2e1feb7ccdbd301。

命令、原始 Map 和 AXF 的哈希与完整符号扫描以 output/q14/symbol_audit.json 和 artifact_sha256.txt 为准。
