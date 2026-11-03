# CM7 FreeRTOS P0 软件验收

基线：main@bffacd28897b47c7e54b90562960ff365f6b7695。独立分支codex/cm7-freertos-p0，不合并main。

## 结论

- 修复Roxy Camera字符串：24B长度字段、有效返回长度及实际数组一致，4B对齐；末PIO Word读取20..23。
- 裸机基线记录160×120 YUY2、目标25 FPS；实测FPS为空，没有伪造实板结果。
- CM7接入官方FreeRTOS V10.6.2 RVDS M7 r0p1 Port，静态LegacyVideo/Idle任务，无内核堆；原Main_Run顺序保持。
- 482项可执行软件用例PASS，44项最终固件/官方源码审查PASS。
- 修正字符串后的裸机CM7、RTOS CM7 UVC、RTOS CM7 TEST、CM4完整重编均0 Error、0 Warning。
- **P0软件验证通过，硬件闸门未验收**：没有实际执行Windows枚举、实板SVC/PendSV/FPU切换、30分钟取流、栈压力和真实FPS对比。

详细实现、优先级、异常入口、栈及CubeMX再生成边界见Docs/FreeRTOS_P0移植说明.md。

## 命令与结果

编译器为ARM Compiler5.06 update7 build960。使用真实ARMCC5机器码，由Unicorn Cortex-M执行；正式Keil构建含官方RVDS Port。

```powershell
python Tests/UsbUvcDescriptors/run_tests.py
python Tests/FreeRtosP0/run_kernel_tests.py
python Tests/VideoConfig/run_tests.py
python Tests/CameraAppInit/run_tests.py
python Tests/UvcBridge/run_tests.py
python Tests/CameraContextRefactor/run_tests.py
python Tests/CameraStreamState/run_tests.py
python Tests/UsbCoreDecoupling/run_tests.py
python Tests/Ep0IrqRace/run_race.py
python Tests/FreeRtosP0/audit_firmware.py
```

| 套件 | PASS / 总数 |
| --- | ---: |
| 描述符及真实Core字符串请求 | 40/40 |
| 最小内核及TIM6时基 | 11/11 |
| VideoConfig | 28/28 |
| CameraAppInit | 13/13 |
| UvcBridge | 35/35 |
| CameraContextRefactor | 72/72 |
| CameraStreamState | 75/75 |
| USB Core | 204/204 |
| EP0 IRQ | 4/4 |
| 可执行用例合计 | 482/482 |
| 另计固件/源文件审查 | 44/44 |

内核11项的输入与期望：NULL/屏蔽中断调用被拒绝；静态启动创建Legacy和Idle并拒绝第二次启动；Idle内存固定；优先级3/2任务延时后切Idle并按Tick唤醒；调度挂起时Tick恢复补账；栈水位/硬件恢复帧对齐与Suspend/Resume；真实Port SysTick与HAL计数独立；APB1/TIMPRE组合推导；TIM6在64/240MHz下重配、Suspend/Resume和优先级；破坏栈底后进入溢出断言Hook。逐项实际结果见results.json及kernel_console.log。

**模拟边界**：首次xPortStartScheduler用明确的测试钩子绕过NVIC异常启动，手动调用vTaskSwitchContext检查内核ready/delay状态；没有执行真实SVC/PendSV异常进入/返回。其余内核、Port栈构造、SysTick C处理、TIM6代码和HAL Cortex NVIC接口直接编译执行。不能把这些PASS等同于实板任务切换成功。

固件审查读取正式AXF实际向量和符号，检查SVC/PendSV/SysTick/TIM6、MSP4KiB、Legacy8KiB、Idle1KiB及DTCM范围、无内核堆、原调用顺序、27个官方文件SHA及CM4排除Group。

本轮没有批量修改历史固定512B的UVC专项；沿用适配当前1023B基线的431项及描述符专项。保留所有本次套件用例，原24项描述符检查未删除，历史wire哈希比较先规范化已接受的25FPS及1023B变化，再逐字节检查原拓扑；字符串预期改为实际名称。CameraAppInit的默认FPS断言从旧10更新到当前25，运行时更新仍须返回不支持。

## 正式构建及内存

命令：`D:/32/UV4/UV4.exe -r MDK-ARM/h7_mac.uvprojx -t <target> -o <log>`。

| 构建 | Error / Warning | Code | RO | RW | ZI |
| --- | --- | ---: | ---: | ---: | ---: |
| 裸机CM7（字符串修正后） | 0/0 | 67070 | 2570 | 708 | 17564 |
| CM7 RTOS UVC | 0/0 | 71334 | 2650 | 788 | 30212 |
| CM7 RTOS TEST | 0/0 | 71334 | 2650 | 788 | 30212 |
| CM4 | 0/0 | 20350 | 914 | 636 | 2580 |

单位B。CM7链接报告的RW+ZI增加12728B，包含静态任务栈/TCB和MSP从1KiB增至4KiB；它不含手工管理的SDRAM帧缓冲。内核堆为零。TEST通过临时修改video_config.c输出选项构建，随后按原字节恢复UVC并完整重编，最终AXF为25FPS目标的UVC配置。

裸机基线在tmp中的git归档副本编译，仅叠加相同字符串修复，不改变正式分支源文件。该临时路径不是长期测试依赖，不提交副本或构建产物。

## 未触碰边界

scope_audit.json对1538个现有文件核对：CM4、HAL/LL、官方USB Core、Camera Device、Media、DCMI/SDRAM/USB BSP、UVC Class/Payload、USB Mode、Application及Common未修改。CM4 Target除同名RTOS Group显式排除外，解析后的配置与原基线相同。新增内核源文件单独保留上游SHA。uvoptx、output/、tmp/保留，不提交。

新增/修改点：字符串c/h；CM7 Platform RTOS和HAL时基文件；main调度器入口、it.c异常唯一所有权、CM7 MSP；Keil Group/Include Path；字符串/默认配置测试；内核及固件审查脚本、报告与移植说明。未实现任何新的Camera/Media/UVC桥接或多任务业务。

首轮诊断曾出现ARMCC5内联汇编U后缀兼容、Keil共享Group默认纳入CM4；均已修复后完整重编。测试桩首轮内存包装函数触发编译器递归内建替换，改为volatile逐字节循环；上下文栈初始保存区包含9个Word，应检查恢复后的硬件帧8字节对齐，而不是要求保存指针自身8字节对齐。没有通过改官方Port修测试。

## 实板待办

1. 用修正字符串的裸机基线确认Windows列出的名称、160×120/25FPS能力和真实成功帧率；记录PCB、固件SHA、DMA错误、丢帧及复位原因。
2. 烧录RTOS分支CM7，保持原CM4固件；观察g_rtos_debug的started、两个Tick、Legacy/Idle迭代、水位和故障字段。HAL Tick通过HAL_GetTick或uwTick观察。
3. 检查SVC/PendSV真实切换、PSP/MSP、FP上下文与1ms Tick，确认Idle获得运行机会。水位每1000轮更新，不等同于每秒精确采样。
4. 执行USB标准枚举、Probe/Commit、Alt1取流、Alt0取消、Reset、Disconnect和Abort失败恢复，核对帧所有权；连续预览至少30分钟，比较真实FPS与DMA错误，并确认CM4 OLED/IPC不退化。
5. 不满足硬件闸门时保持本PR待审核，不推进业务拆任务。
