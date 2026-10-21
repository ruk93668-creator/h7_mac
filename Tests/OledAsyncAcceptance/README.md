# CM4 OLED异步刷新验收

以运行时工作区的 CM4/Device/Src/oled_device.c 为准，通过测试翻译单元直接包含；不使用归档或源码副本。

## 重现

从工程根目录运行：

```powershell
python Tests/OledAsyncAcceptance/run_tests.py
```

测试依赖安装在本目录 python_deps，不改系统包环境。需要重装时：

```powershell
python -m pip install --disable-pip-version-check --target Tests/OledAsyncAcceptance/python_deps unicorn pyelftools
```

使用 ARMCC 5.06 update 7 (build 960)，Unicorn Cortex-M 模式执行独立 ARM 测试 ELF。完整编译命令、退出码与输出见 commands.json。测试程序不加入正式 Target。

正式 CM4 全量构建命令：

```powershell
$p = Start-Process -FilePath 'D:\32\UV4\UV4.exe' -ArgumentList @('-r','D:\32\STM32H7\h7_mac\MDK-ARM\h7_mac.uvprojx','-t','h7_mac_CM4','-o','D:\32\STM32H7\h7_mac\Tests\OledAsyncAcceptance\cm4_keil.log') -WindowStyle Hidden -PassThru
$p.WaitForExit()
```

## Mock契约

- BSP_IIC_WriteAsync 在返回成功前复制传入缓冲区，与当前真实 BSP 的行为一致；不保留 OLED 局部数组指针。
- 模拟只允许一笔活动事务；结果未消费时也拒绝新事务，并记录任何重叠提交。
- Complete 仅使结果就绪，真实 OLED 的 process 必须调用 TakeResult 消费结果后才能发起下一笔。
- 不完成事务时，TakeResult 返回false；可在所有CMD/DATA阶段注入IO、TIMEOUT完成错误，或BUSY、IO、TIMEOUT提交失败。
- 该测试验证 OLED 对 BSP 接口的使用，不执行真实 HAL 或模拟 SSD1306 电气、寄存器、显示效果。

## 输出

- results.md：50例输入、预期、实际及PASS/FAIL。
- results.json：逐步状态、页号、返回值、所有已接受事务的完整字节及越界监控记录。
- oled_async_test.axf：实际执行的测试映像。
- commands.json：实际编译器版本与编译链接命令。
- cm4_keil.log：正式CM4全量编译日志。
- source_before.json/source_integrity.json：1235个正式源文件和配置文件的SHA-256基线与对比。

对象两侧各16字节设保护区并监控CPU读写，正常整帧逐字节校验512字节内容。边界结论针对本次有效状态转换路径，不等同于对任意外部内存损坏的防护承诺。测试不注入非法refresh_page值。
