# IPC V2 软件验收

> 本分支05C-2已升级V3；本套件保留原512项算法/安全场景，仅迁移功能位和剩余保留字段的预期。V2/V3兼容性及新业务验收见../IpcStatus。

本目录编译真实 `Common/Src/ipc_core.c`、`ipc_shared.c`，并包含实际 CM7/CM4 Service 实现。仅 Platform 屏障、短临界区及线程检查使用桩；不复制协议算法。

## 执行

在仓库根目录执行：

```powershell
python Tests/IpcV2/run_tests.py
python Tests/IpcV2/run_build.py
```

最终少量源码修改后，可执行 `python Tests/IpcV2/run_build.py --incremental`。完整重编与增量编译记录分开保存。

工具：ARMCC 5.06 update 7 build 960、armlink、Python 3、pyelftools、Unicorn 2.1.4。脚本优先使用现有 `Tests/OledAsyncAcceptance/python_deps`；没有这些本机依赖时须自行安装，依赖二进制不提交。Keil/ARMCC 路径见脚本，可按本机安装位置调整。

## 覆盖

128个不同场景，分别按CM7/CM4角色及O0/O2优化编译执行，共512次。每次创建独立模拟内存，实际执行ARM指令，避免前一用例污染下一用例。

- `case_catalog.json`：128个场景的编号、说明。
- `results.json`：512次执行结果、失败源代码行及模拟器异常。
- `commands.json`：实际编译/链接命令、版本和退出值。
- `build_results.json`、`*_rebuild.log`：正式工程完整重编。
- `incremental_build_results.json`、`*_incremental.log`：最终4KiB参数隔离保护修改后的正式工程编译链接。
- `verification_report.md`：实现、布局、边界、结果及实板步骤。

100~131在32个真实字段写入点逐一插入读取；200~231在32个真实字段读取点逐一插入发布。测试钩子仅在 `IPC_CORE_TEST` 下编译，正式固件不存在这些钩子。

本模拟器验证控制流、参数边界与交错算法，不证明真实双核总线、DMB执行效果、MPU属性或冷启动时序已经实板通过。Header固定布局在配套冷启动后不变；不模拟热复位恢复。
