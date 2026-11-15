# C2.2B 专项与验证说明

本目录不加入Keil正式工程。production只增加app_camera_consumer.c，不编译本目录夹具。

- run_tests.py：ARMCC5 O0/O2，真实Consumer+Qualification+Tx/Rx/Replay，硬件和释放证明为明示桩。用例17保留并发冷QUERY失败。
- run_business.py：真实App_USB/App_Storage/CameraTask/ControlTask，复用旧依赖桩；介质、硬件、任务运输为桩，不是实板通过。
- run_legacy.py：仅在隔离源码快照执行原测试与断言。legacy_transport_fixture只补齐新增链接依赖，不提供真实归还证明；真实实现强符号优先。
- run_layout.py：仅在已完整构建的隔离快照执行ABI和正式Map/栈比较。
- audit_scope.py：在工作区审计最终生产树、快照、保护文件与原测试不变性。

不要直接在主工作区运行旧回归脚本，以免覆盖原测试报告。最终原始输出保存在output/ConsumerIntegrationC2_2B/下；本目录Evidence保存本轮结果副本，原历史测试/失败记录不删除、不修改。

新专项尚未全绿；Draft PR只供审查，不建议合并。
