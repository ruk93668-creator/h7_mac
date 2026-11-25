# PreMerge-R2 最终注释收尾

**READY_FOR_MERGE_REVIEW**

审核基线为 `aa249bcd79b241d90883da55df3bfdbfa7758bbd`。经验证的源码提交为 `eebd18194408187387bbab43ccc4511e241cbc30`。本报告提交包含相同的源码树。完整候选 SHA 由推送后生成的 `delivery.json` 记录。

## 修改清单

| 路径 | 注释整改 |
| --- | --- |
| `CM7/ApplicationV3/Core/app_services.c` | 补齐服务初始化与队列丢弃通知两个函数指针的用途。 |
| `CM7/ApplicationV3/Runtime/service_mailbox.c` | 以邮箱消息负载约束替换阶段编号。 |
| `CM7/ApplicationV3/Services/Camera/camera_service.c` | 注明物理租约联合体及 Raw、JPEG 两个成员对应的捕获路径。 |
| `CM7/ApplicationV3/Services/USB/usb_service.c` | 用提交与读取退出证明之间的约束说明替换开发阶段引用。 |
| `CM7/Media/Inc/camera_mjpeg_fifo.h` | 说明通用消费者的租约归还规则与内部借用视图的调用限制。 |
| `CM7/Media/Src/camera_mjpeg_fifo.c` | 删除借用结构体前错位的解析器注释，按 FIFO 的 READY 引用转移、登记所有者、会话隔离和最后引用释放更新说明。 |

生产变更限于中文单行注释和一处空行。48 个生产 C/H 文件的记号序列和行数与基线逐文件一致，扫描未发现阶段编号残留或测试标识。两套 CM7 配置各对照 92 个对象，可执行段均逐字节一致，正式 Map 中测试专用符号为零。CM4 HEX 与基线逐字节一致。应用消息、Service 命令、IPC 状态的 ABI 仍为 52 / 16 / 80 B。

## 构建与测试

| 项目 | 结果 |
| --- | --- |
| CM7 正式 JPEG、正式 Raw、CM4、隔离联合板测 | 全部完整 Rebuild，0 Error、0 Warning |
| ARMCC5 O0 / O2 正式 Runtime 和 Service | 每种优化 62/62 PASS |
| ARMCC5 O0 / O2 MediaLease | 每种优化 35/35 PASS |
| ARMCC5 O0 / O2 USB/SD 联合专项 | 每种优化 6/6 PASS |
| JPEG FIFO 历史对照 | 每种优化 13 PASS，历史 FAIL 5、7、8 的断言位置和观测值与原记录一致 |
| 延期实板项目 | NOT_RUN |

正式 JPEG / Raw 的 `video_config.o` 常量分别为 320×240、25 FPS 和 160×120、10 FPS，核实两次 Rebuild 各自采用了预期的构建宏。正式镜像的 Code / RO / RW / ZI 仍分别为 CM7 的 140338 / 3922 / 1296 / 100960 B，以及 CM4 的 26302 / 1438 / 936 / 3576 B。

## 测试隔离和原工程核对

以 `Tests/ApplicationV3/BoardProfiles/prepare.py` 从源码提交导出隔离测试树，重新生成并构建联合板测镜像。注释改动位于补丁上下文之外，重算的测试覆盖层与已提交的 `test_only.patch` 字节一致。测试宏和板测入口只应用于隔离树。

原工程 `D:/32/STM32H7/h7_mac` 中的 CM7、CM4、Common 与正式 `MDK-ARM/h7_mac.uvprojx` 已逐项核对提交树。Keil 用户配置 `h7_mac.uvoptx` 的 SHA-256 与整改前一致。正式重建产物单独归档在 `output`。

证据目录为 `output/ApplicationV3/PreMerge-R2-aa249bc/`。其中包括 `source-review.json`、`machine-review.json`、`Keil/verification.json`、`KeilTest/verification.json`、软件回归原始结果、`FifoHistory/results.json` 及与本轮基线之间的完整 `PreMerge-R2.diff`。

冻结 main 为 `f0ce45fd9ab7250b1e22260e087d606ee5ed9ecb`。最终合并由用户审核决定。
