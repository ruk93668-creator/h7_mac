# VideoConfig 冷启动配置验收

## 本轮修改

业务默认值位于 Application/video_config.c。相机 APP 与 USB 桥接 APP 分别读取默认配置，传入 Device、Media、UVC；删除试作的 video_pipeline，正式 app_main 调度未修改。新增公共参数类型与字节数检查。USB 描述符、Probe/Commit、提交长度与输出节奏使用同一份冷启动配置。

传感器仍限制为已验证的160×120，既有寄存器顺序和采集时序保留。仅配置输出FPS。Media保留40KiB×3容量和原所有权算法；USB UVC仍仅支持YUY2。

## 实际执行

编译器：ARM Compiler 5.06 update 7 build 960。Python 3.11，Unicorn执行ARMCC5生成的真实ARM代码；外设由Mock提供。

```powershell
python Tests/VideoConfig/run_tests.py
python Tests/CameraAppInit/run_tests.py
python Tests/UvcBridge/run_tests.py
python Tests/CameraContextRefactor/run_tests.py
python Tests/CameraStreamState/run_tests.py
python Tests/UsbCoreDecoupling/run_tests.py
python Tests/Ep0IrqRace/run_race.py
```

完整的新增专项编译/链接命令及编译器版本输出见 commands.json。run_tests.py 使用仓库相对路径寻找测试依赖，工具链当前位于 D:/32/ARM/ARMCC/Bin。

| 套件 | PASS | FAIL |
| --- | ---: | ---: |
| 冷启动视频配置及真实USB双帧传输 | 28 | 0 |
| CameraAppInit | 13 | 0 |
| UvcBridge所有权 | 35 | 0 |
| CameraContextRefactor | 72 | 0 |
| CameraStreamState | 75 | 0 |
| USB Core | 204 | 0 |
| EP0 IRQ | 4 | 0 |
| 合计 | 431 | 0 |

配置专项 results.json 提供逐项结果。regression/ 保留相关原有套件的本次执行记录；console日志补充执行输出。

专项包括5/10/15/20/25FPS描述符及控制参数、非法参数、FS包预算、注册顺序、NONE/TEST/ETH/WIFI、重复配置拒绝、源无关UVC尺寸；5/10/20FPS分别实际驱动两帧38包：37×1023B+625B，检查FID/EOF、帧间隔、完成前偏移不推进及最终归还。

原测试中曾用159宽度作为“非法分辨率”夹具，Media解除业务分辨率约束后改为零宽度；非法参数断言保留。相机APP、桥接套件补齐公共配置API及必要初始化桩。未降低端点、Abort或帧所有权断言。

## 正式构建

使用Keil命令 `UV4.exe -r MDK-ARM/h7_mac.uvprojx -t <target> -o <log>` 完整重编。

- CM7 UVC：h7_mac_CM7_keil.log。
- CM7 TEST：cm7_test_keil.log；仅临时把video_config.c输出改为TEST，重编后按原始字节恢复，再完整重编UVC。
- CM4：h7_mac_CM4_keil.log。

三种构建均须在日志中确认0 Error、0 Warning。initial_build_diagnostics.log是初次构建诊断，包含后续已修复的stdbool引用问题和Keil临时via文件问题，不能视为最终结果。

## 范围与限制

scope_audit.json核对main.c、app_main、USB/DCMI/SDRAM BSP、USB Platform、CM4、Drivers、Middlewares相对本轮基线均未修改。它是本轮审查记录，不是长期CI工具。

本次执行上述431项，不宣称历史全部USB专项均已重跑。旧固定512B/76包的历史UVC套件与本轮之前已采用的1023B/38包方案不一致，本轮没有为配置解耦批量改写这些历史断言。

未执行新的实板传感器、Windows取流和不同输出FPS验收。软件通过不保证实际USB带宽或SDRAM并发行为。运行时更新、传感器FPS、任意分辨率、ETH/WIFI均未实现。
