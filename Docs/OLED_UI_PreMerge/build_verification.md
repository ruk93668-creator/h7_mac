# M2 Keil 构建与产物

冻结生产候选 `b4044beec36476b1f327d961bca27d795ffeba90`。以 `git archive` 从已提交内容生成工作副本，用 ARMCC 5.06u7 build 960 全量重建。候选与本地用户修改的 Keil 文件独立。CM4 正式和 Debug 用同一个 Target 的受控配置，Debug 副本仅增加 `OLED_UI2_TEST_INPUT=1,OLED_UI6_TIMING=1`。

| 构建 | Code | RO | RW | ZI | Flash | 静态 RAM | 日志 |
|---|---:|---:|---:|---:|---:|---:|---|
| main CM4 历史同 SHA 基线 | 26302 | 1438 | 936 | 3576 | 28676 | 4512 | [UI1 基线](../UI1/baseline-keil.log.txt) |
| 候选 CM4 正式 | 39646 | 19990 | 976 | 3776 | 60612 | 4752 | [0 错误、0 警告](cm4-build.log.txt) |
| 候选 CM4 Debug | 40658 | 19990 | 992 | 3864 | 61640 | 4856 | [0 错误、0 警告](cm4-debug-build.log.txt) |
| 候选 CM7 原 Target | 137690 | 3114 | 1340 | 100932 | 142144 | 102272 | [0 错误、0 警告](cm7-build.log.txt) |

Flash 为 Code+RO+RW；静态 RAM 为 RW+ZI。正式 CM4 比 main 估算统计增加 **31936 B Flash、240 B 静态 RAM**，其中本轮修复相对 `637d38d` 仅增加 112 B Flash、0 B 静态 RAM。CM7 与 main 无源文件或 Target 变动，与入场候选的同配置构建统计一致（0 B 增量）。未另行重建 main 的 CM7。已知静态调用图深度 668/676 B（正式/调试）并包含未追踪的间接调用；原栈配置 1024 B。实板高水位 NOT_RUN。

- 芯片 `STM32H747XIHx:CM4`，Pack `Keil.STM32H7xx_DFP.4.1.3`。13 个应用、HAL/CMSIS、公共与板级包含目录均为相对路径且存在。首段使用原 CM4 启动文件和 `h7_mac_CM4_custom.sct`。
- 正式宏为 CORE_CM4、USE_HAL_DRIVER、STM32H747xx、USE_PWR_LDO_SUPPLY。MAP 确认正式版本不含 `g_oled_ui_timing`、`g_oled_ui2_debug`，Debug 版两者都在。输出目录为各自项目内的 `h7_mac_CM4/`。
- 保留原 `CMSIS_AGDI` 和 `STM32H7x_2048.FLM` 下载配置。候选 `uvoptx` 没有改变。原 `program_cm4.ini` 指向另一工作树，勿用它烧录。选当前 Keil 工程与 `h7_mac_CM4` Target 中的 Download，或显式使用本候选 HEX。
- 对本地用户修改的 `uvprojx/uvoptx` 做了前后 SHA256 比较，两者字节相同。CM7 Target 段在 main、候选及 Debug 副本间逐字节相同。

| 镜像 | 本机目录 | HEX SHA256 | 范围 |
|---|---|---|---|
| 正式 | `D:\32\STM32H7\h7_mac_ui\OLED-premerge\MDK-ARM\h7_mac_CM4` | `d93349ad6585515105c9992201e61f180b4ae766aa022ed4c991e48768098bed` | 0x08100000–0x0810E91F |
| Debug | `D:\32\STM32H7\h7_mac_ui\OLED-premerge-debug\MDK-ARM\h7_mac_CM4` | `1ab62f50c6cffdf4a95e90b11eef77aef2c578bd3b853c0b8377f05f8ccf1766` | 0x08100000–0x0810ED13 |

HEX 均名为 `h7_mac_CM4.hex`，逐记录通过 Intel HEX 校验，两版及 CM7 的 AXF 验证为 ARM ELF，MAP 与目标地址一致。物理下载、探针连接及实板 NOT_RUN。
