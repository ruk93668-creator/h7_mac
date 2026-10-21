# Device 自动机制隔离测试

运行：

```powershell
& 'D:\32\STM32H7\h7_mac\Tests\DeviceAutoIsolation\run_auto_test.ps1' -PythonDeps '<依赖目录>'
```

依赖：ARMCC5、Python、unicorn、pyelftools。

直接包含当前CM4 device.c和framework_init.c；使用真实DEVICE_EXPORT宏和实际Initcall注册宏。
模拟a/b/c设备，默认auto_init=true/false/true；额外设备用于检查初始化期间动态注册。
测试Scatter仅属于隔离镜像，不改变生产Scatter或Keil配置。
每个用例恢复已初始化的ELF执行节并使用全新仿真器；错误导出指针只在仿真Flash中注入。

auto_results.md/auto_results.json提供逐项结果；缺陷与补充建议.md解释风险。
run_auto_test.py会把可复现的重入问题记录为Fail；诊断脚本退出0只表示诊断执行完成，
不能解读为所有用例通过。未知断言失败或仿真异常仍使脚本失败。

本次未修改生产代码、未烧录、未验证实际外设或正式Target完整链接。
