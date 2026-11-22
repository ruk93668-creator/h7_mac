# BLE B1 UART验证

实现与实板步骤见 [B1交接报告](../../Docs/BLE/BLE_B1_Implementation.md)。

`python run_contract.py` 在 ARMCC5+Unicorn 中执行真实驱动，结果在 results.json；需要本目录 python_deps 下的 unicorn 和 pyelftools。

`python run_build.py` 对照构建当前/不含B1的CM7，记录 build_results.json；不烧录。

本目录保留测试源码、命令、结果和构建日志；不提交依赖或二进制。仿真通过不等于实板串口吞吐或可靠性验收。
