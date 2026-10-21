# CM4 BSP_IIC_Init 实现与验收

日期：2026-10-16。

## 修改点

- 新增CM4/BSP/bsp_iic.h：仅公开SysResult_t BSP_IIC_Init(void)，不向上层暴露HAL句柄。
- 新增CM4/BSP/bsp_iic.c：独立初始化I2C2，提供应用层唯一的I2C2 MSP Init/DeInit实现。
- Keil CM4 BSP Group加入源文件和头文件；复用已有Include Path，CM7保持禁用该组。
- BSP通过FRAMEWORK_INIT_REGISTER注册INIT_LEVEL_BUS；没有新增main中的直接调用。
- 未改动Core生成文件、CM7源码、HAL、Device、生产Scatter或IOC。

## 初始化过程

```text
Framework_InitRuntime
    ↓ BUS
BSP_IIC_Init
    ├─ HAL_RCCEx_PeriphCLKConfig：I2C123选择D2PCLK1
    ├─ HAL_RCCEx_GetPeriphCLKFreq：检查I2C2内核名义频率为120 MHz
    ├─ 配置私有hi2c2：Timing=0x307075B1、7位地址等
    ├─ HAL_I2C_Init
    │    └─ HAL_I2C_MspInit：GPIOB时钟、PB10/PB11 AF4开漏、I2C2时钟
    ├─ 模拟滤波：启用
    └─ 数字滤波：0
    ↓ BUS成功后进入DEVICE等级
Device_InitAll
```

GPIO使用GPIO_NOPULL、GPIO_SPEED_FREQ_LOW，与已审核的CubeMX配置一致；需由实物电路提供适当外部上拉。
I2C1/2/3共享内核选择器，今后添加其他I2C实例时必须统一时钟策略。

## 返回与重复调用

| 条件 | 返回 |
|---|---|
| 初始化成功 | SYS_OK |
| HAL_BUSY | SYS_ERR_BUSY |
| HAL_TIMEOUT | SYS_ERR_TIMEOUT |
| HAL_ERROR或其他异常HAL状态 | SYS_ERR_IO |
| 内核名义频率不是120 MHz | SYS_ERR_INVALID_STATE，拒绝套用固定Timing |
| 初始化过程中重入 | SYS_ERR_BUSY |
| 已成功初始化且频率保持正确 | SYS_OK，不重复配置硬件 |

HAL初始化或滤波配置失败后调用HAL_I2C_DeInit，释放部分配置并保留首个错误码；后续重试重新运行MSP初始化。
RCC配置在BSP函数内完成并检查状态，避免void MSP回调无法向调用者直接返回RCC失败。

## 验收结果

- 正式CM4 Target ARMCC V5.06 update 7全量重建：0 Error、0 Warning。
- HAL桩隔离测试21/21通过：硬件步骤顺序、配置值、错误频率、各HAL阶段的ERROR/BUSY/TIMEOUT、失败清理及重试、重复调用、初始化重入、MSP对象过滤、BUS/DEVICE先后和Fail-fast。
- 实际AXF的ER_FW_INIT中存在BSP_IIC_Init注册项，等级1即BUS；函数指针匹配。
- 实际AXF中HAL_I2C_MspInit、HAL_I2C_MspDeInit均解析为应用强符号；应用源码没有第二份MSP或MX_I2C2_Init调用。
- 原有文件哈希检查：仅Keil工程文件因加入BSP条目而变化；原Core、Device、HAL、CM7和Scatter未变。

证据目录：D:/32/STM32H7/h7_mac/Tests/BspIicIsolation/。
包含cm4_keil.log、results.md/json、link_integrity.json及可复现测试脚本。

## CubeMX与硬件边界

- 手写实现全部位于独立BSP文件，不写入生成的Core文件。
- 后续CubeMX生成若重新加入i2c.c，必须检查并排除其中重复的MSP/初始化实现，不能让两套I2C2所有者同时编译；main中也不能再次初始化。
- 本轮只实现初始化；收发接口、SSD1306命令和显示业务没有加入BSP。
- HAL桩测试验证控制逻辑，不验证电气行为。频率检查依据RCC配置及HAL计算值，不是测量实际振荡器；未烧录或上板测量SCL。
