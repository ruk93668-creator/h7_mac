# Camera Application 初始化入口验收

正式源码：CM7/Application/app_camera.c/.h。公共Prepare为.c内部函数，不导出。

- Prepare成功后不重复初始化OV5640和Media。
- 传感器已成功、Media失败时仅重试Media；启动失败保留公共初始化结果。
- Init启动Snapshot；InitStream启动Stream；返回Media实际结果。
- 正在采集、有未释放帧或故障未恢复时，由Media拒绝重复/交叉启动。
- 完成Stop及帧释放、恢复NONE/IDLE之后，允许重新启动或切换模式。
- API仅由主循环串行调用，不提供ISR调用或并发初始化保证。
- 原App_Init仍调用App_CameraInit，没有改成自动连续采集。

命令：

```powershell
python Tests/CameraAppInit/run_tests.py
python Tests/CameraContextRefactor/run_tests.py
```

ARMCC5编译实际app_camera.c，Unicorn执行12项应用测试，12/12 PASS。Device和Media为测试桩，核对配置复用、调用次数、失败重试及返回值传递。实际Media通过既有CameraContextRefactor 72/72回归验证，涵盖三种格式、重复启动、交叉启动、READY/DRAINING、故障恢复和所有权。

CM7、CM4使用Keil -r正式工程完整重编，均0 Error、0 Warning，日志在本目录。未操作实板，不证明实际传感器、DMA和USB图像链路通过。
