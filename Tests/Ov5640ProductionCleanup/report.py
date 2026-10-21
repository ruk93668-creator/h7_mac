from pathlib import Path
import sys,json,shutil
sys.path.insert(0,str(Path('Tests/OledAsyncAcceptance/python_deps').resolve()))
from elftools.elf.elffile import ELFFile
p=Path('Tests/Ov5640ProductionCleanup')
with Path('MDK-ARM/h7_mac_CM7/h7_mac_CM7.axf').open('rb') as f:
 elf=ELFFile(f);symbols={s.name for s in elf.get_section_by_name('.symtab').iter_symbols()}
 audit={'old_test_symbols_present':[s for s in ['g_camera_report','s_verify'] if s in symbols],'production_config_present':'Ov5640_ConfigureY8Colorbar' in symbols,'application_entry_present':'App_CameraInit' in symbols}
(p/'link_audit.json').write_text(json.dumps(audit,indent=2),encoding='utf-8')
for name in ['cm4_results.json','cm4_results.md','cm4_commands.json']:shutil.copyfile(Path('Tests/Ov5640Migration')/name,p/name)
print(audit)
report='''# OV5640 正式初始化清理验收

## 修改点

仅修改四个正式文件：

| 文件 | 修改 |
|---|---|
| CM7/Application/app_camera.c | 删除 g_camera_report 和报告写入；保留设备查找、Ops 检查、配置调用、错误返回，以及成功后避免重复初始化的私有状态 |
| CM7/Application/app_camera.h | 删除 AppCameraReport_t 及报告变量声明，仅公开 App_CameraInit |
| CM7/Device/Inc/ov5640_device.h | 删除 Ov5640ConfigReport_t、OV5640_VERIFY_COUNT；配置 Ops 不再接收测试报告参数 |
| CM7/Device/Src/ov5640_device.c | 删除 17 项寄存器回读表、比较循环及测试报告；保留芯片 ID 检查、SCCB 适配、完整组件初始化、160×120 Y8、原行帧时序和内部彩条输出 |

正式代码保留简短中文 // 注释、命名成员绑定及文件前部操作表组织方式。源码仍为 GBK。

## 正式启动路径

原双核 Bootstrap → Framework BUS 初始化 BSP → Device 自动注册/初始化 → APPLICATION 的 App_Init → App_CameraInit → configure_y8_colorbar。

Device 检查芯片 ID 为 0x5640，然后配置传感器。真实通信及 ID 检查失败仍返回原 SysResult_t，交给 Framework Fail-fast 和 main 的 init_error 处理。

Application 不再保存芯片 ID、尺寸、逐项验收状态等报告。配置成功后仍避免重复启动；失败后允许显式重试。

## 独立验收

17 项寄存器验收已移至本目录 camera_test.c 的 s_test_verify 和 Test_VerifyRegisters，报告也仅保存在隔离测试程序中；它们不在 Keil 正式 Target 中。

编译当前真实 BSP、Device、Application、组件及 Framework；GPIO 边沿模型解码真实软件 SCCB，模拟传感器寄存器。先运行正式 Framework_InitRuntime，再由测试程序主动执行 17 项回读。

42 项模拟测试、509 项断言全部通过：完整启动、17 处逐项回读不一致、验收读取 NACK、原 SCCB 读写/NACK/总线忙、复位等待、多字节适配、ID 错误、写表失败、失败重试、重复初始化、缺失设备/Ops、PLL/DWT 异常及回绕。测试额外确认：彩条开启后、独立验收开始前，正式初始化没有验收读事务。

回读不一致或验收读取失败时，独立测试准确返回 PROTOCOL/IO；正式 Initcall 保持成功。这是本次分离的预期行为。真实写表失败及芯片 ID 错误仍使正式初始化失败。

CM4 OLED 回归：12/12 PASS，3493 项断言。该回归编译当前 CM4 Application、OLED、Device、Timer、Event、Runtime 和 Initcall；I2C 通信由桩模拟。

历史 Tests/Ov5640Y8 报告及用例保留，描述的是清理前带报告接口版本。本目录为当前正式接口的可运行验收入口。

## 命令

```powershell
python Tests/Ov5640ProductionCleanup/run_tests.py
python Tests/Ov5640Migration/run_cm4_regression.py
D:\\32\\UV4\\UV4.exe -r D:\\32\\STM32H7\\h7_mac\\MDK-ARM\\h7_mac.uvprojx -t h7_mac_CM7 -o D:\\32\\STM32H7\\h7_mac\\Tests\\Ov5640ProductionCleanup\\cm7_keil.log
D:\\32\\UV4\\UV4.exe -r D:\\32\\STM32H7\\h7_mac\\MDK-ARM\\h7_mac.uvprojx -t h7_mac_CM4 -o D:\\32\\STM32H7\\h7_mac\\Tests\\Ov5640ProductionCleanup\\cm4_keil.log
```

编译器为 ARMCC 5.06 update 7（build 960）；完整隔离测试编译/链接命令及输出见 commands.json。每项输入、预期及实际结果见 results.md/json。

## 编译和保护检查

- CM7 正式 Target 完整 Rebuild：0 Error、0 Warning。
- CM4 正式 Target 完整 Rebuild：0 Error、0 Warning。
- 正式 CM7/CM4/Common 全局搜索已无旧报告类型、变量和 17 项验收表引用。
- 正式 CM7 AXF 不含 g_camera_report、s_verify；保留 App_CameraInit 和摄像头配置函数。
- 对照修改前哈希，仅上述四个正式文件变化。CM4、Framework、Common、BSP、ST 组件、双核启动、Scatter、Keil 工程配置均未改变。

未开发或启动 DCMI、DMA、SDRAM。没有下载或实板操作；真实供电、XCLK/PCLK、电气时序、真实寄存器回读及图像输出仍未验证。
'''
(p/'验收报告.md').write_text(report,encoding='utf-8')
