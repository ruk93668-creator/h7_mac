# STM32H747 h7_mac

STM32H747双核裸机工程，使用Keil ARMCC5和CubeMX配置。

- CM7：OV5640软件SCCB、Y8/RGB565/YUV422配置、DCMI Snapshot、SDRAM及Camera Media。
- CM4：I2C2、SSD1306 OLED设备与应用。
- Common：公共配置、接口、错误码、IPC协议及Framework工具。
- Framework：Initcall、事件、软件Timer及主循环调度。

## 目录

| 目录/文件 | 内容 |
|---|---|
| `CM7/`、`CM4/` | 各核Application、Device、Framework、Service、Platform、BSP及Core |
| `Common/`、`Board/` | 公共接口与板级配置 |
| `Drivers/` | STM32 HAL与CMSIS，保留原有许可证 |
| `MDK-ARM/` | Keil工程、启动文件及自定义Scatter |
| `h7_mac.ioc` | CubeMX工程配置 |
| `Tests/` | 独立模拟测试、实板导出图像与验收记录 |
| `Docs/` | 架构及阶段记录，具体状态以当前源码和对应验收报告为准 |

## 编译

打开`MDK-ARM/h7_mac.uvprojx`，分别选择`h7_mac_CM7`与`h7_mac_CM4`。当前使用ARM Compiler 5.06 update 7 build 960。

构建产物、Python依赖运行库和本机界面文件不纳入版本控制。部分独立测试脚本使用本机ARMCC工具链路径及Unicorn/pyelftools依赖，运行前需按环境调整；PNG测试另需Pillow。

## 说明

工程包含动态三缓冲状态机的开发和隔离测试。隔离测试通过不等同于Continuous DMA DBM实板验收完成；请参考相应Tests报告。

CubeMX重新生成代码前应检查差异，保留自定义模块和USER CODE区域。第三方驱动遵循各自目录中的许可证，本项目不另行覆盖第三方许可。
