# QSPI Q1.1 纯软件验证

审核基线 `ce27b00062cba53ea90f137acc17ee360b8383ee`。不下载、不操作探针或 Flash。
需要 Windows、ARMCC5（D:/32/ARM/ARMCC/Bin）、UV4（D:/32/UV4）、Python 3.11。
依赖安装到独立输出目录：`python -m pip install --target output/QspiFlashQ1/python_deps unicorn==2.1.4 pyelftools==0.33`。

执行顺序：

1. `python Tests/QspiFlashQ1/run_build.py baseline`：git archive 基线后，隐藏运行正式 CM7/CM4 重建。
2. `python Tests/QspiFlashQ1/run_tests.py`：ARMCC 编译包含生产 bsp_qspi.c 的测试，用真实 CMSIS 布局/屏蔽指令和 HAL Mock 在 Unicorn 运行。ISR 号由测试宏注入，NVIC/MMIO 用仿真内存；不是外设时序仿真。
3. `python Tests/QspiFlashQ1/run_hal_link.py`：链接真实 ST QSPI/GPIO/MDMA 和 BSP；只执行真实 HAL 首次 Init 的默认超时路径。MDMA 是原厂 Abort 的链接依赖，BSP 不启用或调用 DMA 传输。
4. `python Tests/QspiFlashQ1/run_build.py current verified`：在独立基线快照仅覆盖四份批准生产文件，按原正式配置重建双核工程。
5. `python Tests/QspiFlashQ1/audit_scope.py`：CM4 XML 不变、CM7 仅三文件条目、HAL 开关和禁写范围检查。
6. `python Tests/QspiFlashQ1/run_regression.py`：基线/verified 快照内运行三组未修改历史测试，保留已有失败；返回非零表示套件确有失败，不代表 QSPI 新增失败。详见 comparison.json。
7. `python Tests/QspiFlashQ1/collect_evidence.py`：复制可审核日志/JSON，并生成 SHA256 清单。不提交 AXF、对象文件、运行库。

构建脚本拒绝覆盖已有同名快照，复跑时使用新的 label；历史回归默认读取 baseline/verified。输出仅写 `output/QspiFlashQ1/` 和其隔离副本，审核用文本证据冻结于本目录 evidence/。

本次 BSP Mock 为 51 个用例、981 条断言，全部通过；真实 HAL 测试独立通过。历史三组回归均存在基线同样的失败，不能标记全套回归 PASS。证据和限制见 `Docs/QSPI/Q1_1_SoftwareReport.md`。
