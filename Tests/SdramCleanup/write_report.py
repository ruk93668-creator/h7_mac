from pathlib import Path
p=Path('Tests/SdramCleanup')
report='''# SDRAM 正式固件诊断清理验收

## 结果

用户已反馈清理前SDRAM通过真实硬件测试。本轮保留该初始化配置，仅移除正式固件诊断。没有重新下载或执行实板测试。

- CM7、CM4正式Target完整重编译：0 Error、0 Warning。
- 正式初始化隔离模拟：20/20 PASS，531项断言；所有用例对SDRAM数据区均为零读写。
- 保留的独立1KB诊断：23/23 PASS，1058项断言。
- OV5640当前源码回归：42/42 PASS，464项断言。
- CM4 OLED回归：12/12 PASS，3493项断言。
- 编译器：ARMCC 5.06 update 7（build 960）。

## 修改点

| 文件 | 修改 |
|---|---|
| CM7/BSP/bsp_sdram.c | 删除g_sdram_init_result、g_sdram_sdclk_hz及记录操作；BSP_SDRAM_Init直接返回Sdram_Init结果 |
| CM7/BSP/bsp_sdram.h | 删除诊断全局变量声明；保留地址、窗口、时钟常量、初始化和Ready接口 |
| 原CM7/Diagnostics/sdram_check.c/.h | 移到Tests/SdramV0/diagnostics，正式源码目录不再保留；1KB图案、备份恢复及统计变量仅在独立测试中存在 |
| MDK-ARM/h7_mac.uvprojx | 两个Target移除CM7 Diagnostics Group；移除其Include Path |
| MDK-ARM/h7_mac.uvoptx | 移除诊断Group和已删除的g_sdram_* Watch项；保留其他调试配置 |
| Tests/SdramV0/sdram_test.c、run_tests.py | 修正诊断文件移动后的引用，移除对已删除BSP诊断变量的依赖；保持独立测试可编译 |
| Tests/SdramCleanup | 新增本轮正式无诊断启动、独立诊断、摄像头和OLED回归记录及报告 |

原验收报告和时钟查询修复记录保留在Tests/SdramV0；新增README说明其历史固件观察变量已从正式代码删除。

## 保留的初始化

FMC Bank2、V0引脚、PLL2 660MHz VCO/PLL2R 220MHz/SDCLK 110MHz、CAS2及其时序、839刷新计数、CLK_ENABLE/1ms/PALL/8次自动刷新/模式0x0220全部不变。

保留FMC mux检查及真实HAL_RCCEx_GetPLL2ClockFreq校验，不再使用不支持FMC的通用时钟查询。保持PLL2占用拒绝、时钟条件检查、HAL错误/超时转换、初始化成功幂等处理。

MPU Region2及其调用点未改：0xD0000000～0xD000FFFF，Normal、Non-cacheable、Shareable、可读写、禁止执行。Region0/1、Cache状态、Scatter不变。

BSP_SDRAM_Init仍通过BUS Initcall自动初始化；正式MEDIA阶段没有SDRAM测试Initcall。正式启动不再写图案、备份/恢复数据或分配1KB测试备份数组。原首1KB不再被启动测试使用；没有增加Framebuffer或改变链接分配。

## 错误处理

BSP_SDRAM_Init返回SysResult_t → Framework_InitLevel/Framework_InitRuntime保留首次失败 → main的init_error接收结果。内部s_sdram_ready/s_clock_ready/s_msp_ready属于初始化状态，继续保留，不作为测试统计。

沿用原main错误策略，未新增恢复或故障策略。已删除g_sdram_*正式观察变量；需查看初始化结果时，在Framework_InitRuntime返回处观察main的init_error，或调试BSP_SDRAM_Init的返回值。

## 验收证据

1. audit.json：对照本轮基线，仅上述四个修改文件和两份移出文件变化。main、MPU Region2、双核启动、IPC、CM4、OV5640、Framework、Common、Board和Scatter哈希未变。
2. 正式源码及工程配置搜索：无g_sdram_*、SDRAM_CheckRun、sdram_check、Diagnostics引用。
3. link_audit.json：正式CM7 AXF无诊断函数、测试Initcall、统计变量或备份数组；保留BSP_SDRAM_Init及其BUS注册、MPU_ConfigSdram。
4. production_results.md/json：编译当前真实BSP、main MPU函数、Framework，隔离程序不编译诊断模块。20项覆盖正常启动、PLL/源/分频/频率/mux、HAL/MSP/各命令/刷新错误和超时、重复初始化；所有场景监测到SDRAM数据区零访问。
5. diagnostic_results.md及results.json：单独编译Tests中的1KB诊断和当前真实BSP，保留原图案、恢复、错误注入、边界及重入验收。
6. camera_results.md/json、oled_results.md/json：当前摄像头和OLED功能回归。

时钟查询函数从当前HAL源码原文提取编译；PLL2配置、GPIO、SDRAM命令及MPU接口使用Mock。模拟不能覆盖真实电气、刷新保持或温压裕量。

## 命令

```powershell
python Tests/SdramCleanup/run_production.py
python Tests/SdramCleanup/run_tests.py
python Tests/SdramV0/Camera/run_tests.py
python Tests/Ov5640Migration/run_cm4_regression.py
python Tests/SdramCleanup/audit.py
D:\\32\\UV4\\UV4.exe -r D:\\32\\STM32H7\\h7_mac\\MDK-ARM\\h7_mac.uvprojx -t h7_mac_CM7 -o D:\\32\\STM32H7\\h7_mac\\Tests\\SdramCleanup\\cm7_keil.log
D:\\32\\UV4\\UV4.exe -r D:\\32\\STM32H7\\h7_mac\\MDK-ARM\\h7_mac.uvprojx -t h7_mac_CM4 -o D:\\32\\STM32H7\\h7_mac\\Tests\\SdramCleanup\\cm4_keil.log
```

详细隔离编译及链接命令见production_commands.json、commands.json、camera_commands.json、oled_commands.json。完整正式构建输出见两核keil.log。

没有开发或启用DCMI、DMA采集；没有改变摄像头、OLED、IPC或双核启动逻辑。
'''
(p/'验收报告.md').write_text(report,encoding='utf-8')
Path('Tests/SdramV0/README_正式固件已清理.md').write_text('''# SDRAM诊断已从正式固件移出

用户反馈110MHz实板测试通过后，已清理正式固件诊断。历史验收报告及clock_fix保留。

sdram_check.c/.h现在位于diagnostics/，仅供独立测试；g_sdram_*观察变量及SDRAM_CheckRun自动Initcall已不在正式Target中。正式BSP仍在BUS阶段初始化SDRAM，错误由SysResult_t和Framework返回main的init_error。

本目录run_tests.py仍可执行独立1KB诊断；本轮完整结果和新增“正式初始化零数据区访问”验收见../SdramCleanup/验收报告.md。
''',encoding='utf-8')
print('report written')
