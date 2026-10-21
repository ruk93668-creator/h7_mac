# CM4 Device BUSY 定向测试

```powershell
& 'D:\32\STM32H7\h7_mac\Tests\DeviceBusyAcceptance\run_busy_test.ps1' -PythonDeps '<依赖目录>'
```

依赖ARMCC5、Python、unicorn、pyelftools。
使用Tests/DeviceAutoIsolation/auto_test.sct作为隔离链接布局；不改生产Scatter。
实际Device和Framework源码被测试翻译单元直接包含，生命周期采用模拟回调。
每个用例从新仿真状态开始，按ELF执行节恢复初始化内存。

验收总结.md列出结论、全部Warning和正式AXF导出项证据。
busy_results.md/json为逐项仿真结果，cm4_keil.log为正式Target全量重建日志。
formal_export.json为正式AXF静态导出表检查，source_integrity.json为生产文件哈希检查。
