# 模拟设备验收

使用当前指定核心 Device 实现验证 Register→Find→Init→Start→Stop。
仅新增隔离测试，不加入 Keil Target，不修改生产源码。

运行：

```powershell
& 'D:\32\STM32H7\h7_mac\Tests\DeviceIsolation\run_device_test.ps1' -PythonDeps '<Python依赖目录>'
& 'D:\32\STM32H7\h7_mac\Tests\DeviceIsolation\run_device_test.ps1' -Core CM4 -PythonDeps '<Python依赖目录>'
```

依赖：ARMCC5、Python、unicorn、pyelftools。
对象和AXF默认生成在临时目录；编译命令、日志和报告保存在本目录。

模拟回调检查设备对象指针、调用前状态、调用次数及顺序，并可返回指定失败码。
模拟设备采用静态存储；strcmp由隔离标准库桩提供。
仿真器按ELF节的执行地址恢复初始化数据，模拟C运行时初始化完成后的状态；
每个用例均使用全新仿真实例，避免设备链表和回调计数残留。

报告：device_results.md；机器可读结果：device_results.json。
生产文件完整性：source_integrity.json。

默认运行CM7；指定-Core CM4时使用实际CM4源码，结果保存到CM4子目录。
隔离仿真不代表上板测试；Keil Target编译结果另见相应构建日志。
