# BSP IIC 隔离测试

```powershell
& 'D:\32\STM32H7\h7_mac\Tests\BspIicIsolation\run_test.ps1' -PythonDeps '<依赖目录>'
```

依赖ARMCC5、Python、unicorn、pyelftools。
测试直接包含实际CM4 BSP及Framework Initcall代码，以隔离HAL头文件和HAL桩注入成功、失败、时钟和重入条件。
桩只用于隔离测试，不加入正式Keil Target。正式Target使用真实HAL头文件和HAL库全量编译。
每个用例使用全新仿真器，按ELF执行节恢复初始化内存。
链接布局使用Tests/ResultMigration/result_test.sct，未修改生产Scatter。

results.md/json及execution_output.txt记录21项测试；cm4_keil.log记录正式构建结果。
link_integrity.json记录真实AXF中Initcall及MSP绑定与生产文件变化范围。
