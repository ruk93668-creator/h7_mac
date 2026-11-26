# Ethernet N1–N5 最终交付

**READY_FOR_HARDWARE_ACCEPTANCE**

审查日期：2026-11-26（Asia/Shanghai）。源码冻结：`78580073610ff4cfe06a4b004b1b0d75bffef499`；main：`851c1115fe097f7fd2e0bdcaa5f7b3f8c03feb71`。

正式Network任务、唯一lwIP上下文、启停/重启、Camera消息消费者、私有JPEG和Keil生产接入已经实施。
最终干净源码十四固件全部0 Error/0 Warning；六组Ethernet、170/170生产场景及相关主线回归通过。
旧IPC Status仍为FAIL/BASELINE_FAIL，与main同因，未改旧IPC或旧测试制造通过。

浏览器RST遗留申请、停止态timer及恢复首错有源码修正和前后证据。
早期168/170检查点及“通过”表述勘误保留，未改变断言等待时间。
原工作区和RTE保护，CM4/500ms、USB、Storage、Media、BSP及上游lwIP保留。
源码、架构、预算、完整Diff、MAP、SHA、日志及验收操作在本目录和交付包。

软件READY不等于硬件PASS或READY_FOR_MERGE。
实际MPU/DCache/DMA、100轮、60分钟、历史FIFO Error和20FPS由用户实板验收。
没有烧录、创建PR、修改远端main或合并main；完成后等待用户验收及独立审核。
