# ApplicationV3 旧 CM7 应用清理

## 状态

源码清理、V3 专项和原 Keil 双核正式 Rebuild 均通过。清理基线为 `a7974b13276935f42fb3470230df43dc3e5e8e8b`。只在 `codex/application-v3-flipper` 的主工作目录操作。

## 源码清单

| 目录 | 清理前 | 清理后 |
| --- | ---: | ---: |
| CM7/Application | 39 | 0 |
| CM7/ApplicationV2 | 0 | 0 |
| CM7/ApplicationV3 | 9 | 22，包括 12 C / 10 H |

用 `git mv` 迁移 13 个基础设施文件。Core 包括 `app_events.c/.h`、`app_messages.c/.h`、`app_tasks.c/.h`、`app_status.c/.h`、`video_config.c/.h` 和 `app_main.h`。Tasks 包括 `ipc_task.c/.h`。新增 `service_tasks.h` 汇总现有任务创建接口。原 Storage 头中仍需要的配置结构迁到视频配置头，V3 生产头文件无需引用旧录像或帧交接类型。

删除 26 个旧目录文件。目录级清单以附带的路径清单为准，主要包括四个 Legacy 业务任务实现和头、旧 Camera/USB/Storage 业务、Bridge、Frame Dispatcher、旧 `app_main.c` 拼接槽和 Lifecycle/Replay/Qualification。另删 V3 旧独立 Keil 工程生成器。未清理原 output、历史失败证据和用户本地 Keil 配置。没有新建工程副本或全量备份。 [文件变更清单](D:/32/STM32H7/h7_mac/output/ApplicationV3/LegacyRemoval/source-inventory.txt)

```text
CM7/ApplicationV3/
  Core/       App Context、Initcall、静态 Service、队列、事件、BOOT、状态、视频配置
  Services/   Camera、USB、Storage
  Tasks/      四个服务任务入口、原 IPC 任务
```

产品保留原 5 条静态消息队列、任务通知和 BOOT 屏障。旧 C1 的三条 Lifecycle 队列、收发及 Initcall 已移出。旧 56B Lifecycle 消息的历史断言由历史测试保留。V3 测试验证实际使用的 AppMessage 52B、frame 偏移 28B，以及 CM4 IPC 的 80B 应用状态。V3 软件专项编译真实 V3 消息源码与原 FreeRTOS Queue/List，调度、任务身份和设备结果由测试控制。依赖已删除生产入口的旧专项为历史测试，本轮未执行。

## 双核和诊断链接

| 检查 | 实际结果 |
| --- | --- |
| V3 独立翻译单元 | ARMCC5 O0/O2 各 12/12，无诊断 |
| V3 启动专项 | O0/O2 各 7/7 |
| 原 Keil CM4 全量 Rebuild | 退出 0，0 Error、0 Warning |
| 原 Keil CM7 全量 Rebuild | 退出 2，19 Error、0 Warning。ARMCC5 `#5 cannot open source input file` |
| CM7 诊断链接 | 原 ARMCC5 链接参数及本次可用对象，剔除旧登记后 armlink 退出 0。该 Map 仅用于鉴别项目清单问题 |

[独立编译](D:/32/STM32H7/h7_mac/output/ApplicationV3/LegacyRemoval/V3Compile01/results.json) · [V3 专项](D:/32/STM32H7/h7_mac/output/ApplicationV3/LegacyRemoval/Startup01/results.json) · [CM7 原始日志](D:/32/STM32H7/h7_mac/output/ApplicationV3/LegacyRemoval/Build01/h7_mac_CM7.log) · [CM4 原始日志](D:/32/STM32H7/h7_mac/output/ApplicationV3/LegacyRemoval/Build01/h7_mac_CM4.log) · [CM7 诊断 Map](D:/32/STM32H7/h7_mac/output/ApplicationV3/LegacyRemoval/CM7DiagnosticLink/ApplicationV3_Diagnostic.map)

最终 CM7 Code / RO / RW / ZI 为 67414 / 2830 / 840 / 94728 B。对比清理前原工程 V3 的 67462 / 2938 / 836 / 95652 B，Flash 按 Code + RO + RW 由 71236 B 降至 71084 B，静态 RAM 按 RW + ZI 由 96488 B 降至 95568 B。正式 Map 对照了 12 个 V3 独立对象、唯一 \`App_Init\` 与每个任务创建入口，未发现旧 Lifecycle、Qualification、Legacy Bridge 或 V2 Runtime 符号。

最终 CM4 Code / RO / RW / ZI 为 26302 / 1438 / 936 / 3576 B。HEX 与基线逐字节一致，SHA256 为 \`3030484c03d4983a192be39142dce7b6dfdb8e6c93a49e1f8285ee846540fb17\`。 [最终双核验收](D:/32/STM32H7/h7_mac/output/ApplicationV3/LegacyRemoval/Build02/verification.json) · [CM7 正式 Map](D:/32/STM32H7/h7_mac/output/ApplicationV3/LegacyRemoval/Build02/h7_mac_CM7.map) · [CM4 正式重编日志](D:/32/STM32H7/h7_mac/output/ApplicationV3/LegacyRemoval/Build02/h7_mac_CM4.log)

Camera、USB、Storage、Control 和 IPC 的栈配置仍为 1536、2048、2048、768、512 个 word，优先级仍为 3、4、1、2、1。没有调整双核共享 IPC、驱动、IRQ 或 FreeRTOS 配置。

## 原 Keil 文件登记

原 \`h7_mac_CM7\` 的 \`Application V3\` 组登记了 12 个独立 C 编译单元和对应头文件。Include Path 指向 V3，旧选择宏和 C 文件拼接入口已移除。工程内没有旧 Application 文件登记。

初次尝试保持 CM4 Target 的原冻结清单，µVision 把 CM4 排除分组中的旧条目归入 CM7，产生 19 个旧路径不存在错误。经单独批准，仅同步 CM4 排除分组的名称和文件登记，V3 在 CM4 中全部保持排除。CM4 活动编译单元与编译选项逐项一致，再以 0 错误 Rebuild 与 HEX 对照验收。保留了原本存在的用户 Keil 本地改动。提交中的工程文件从 Git 基线推导，与实际重编的工作树工程具有相同的编译主选项和活动源文件集合。

仅提交 V3 分支的清理与验收内容。main、历史测试、实板证据和历史 output 保持原样。运行期栈高水位、实际 Camera 采集、UVC 视频及 SD 录像均为 NOT_RUN。停在本轮清理，等待人工审核。
