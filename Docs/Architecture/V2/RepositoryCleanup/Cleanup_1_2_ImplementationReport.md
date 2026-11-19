# Cleanup 1.2 旧资格服务提取

基线 40a72ef51bef17bc19c60b39d7e66a3ec3ec4169。产品与历史夹具提交 63086c35a89d98a936c2337e341ddd4be16bbe2e。

- 旧资格核心、私有适配和布局已移至 Tests/LegacyLifecycle/Source。产品 app_camera_qualification.c 保持原路径，App_QualificationNextGeneration 函数体与基线逐字节相同。
- Legacy START、Media/Device 停止、同轮停止保护、Bridge、ACK/FAULT 和首错继续由原业务函数执行。仅移出六个旧资格字段、资格回调、Ops 和专用 STOP 结果记账。
- 历史夹具从当前产品 app_camera.c 生成副本并只插入旧资格字段和适配。如果精确锚点缺失或歧义，构建会失败。R2 编译当期 CameraTask、CameraCapture 和 START/STOP 函数。BSP、Bridge Idle 和租约边界仍使用原可控夹具。B1 Integration 另执行真实 Bridge/Storage 联合路径。
- 两个 P8.1/P8.2 测试只更新 include，保留原断言和陷阱。测试和生产的依赖方向保持单向。
- O0/O2 原结果为 C2.2A 162/162、R2 74/74。基线和候选逐项完全一致。R2 原 C2.2A 包装器接入已有未绑定钩子后也为 162/162，原始失败日志保留。
