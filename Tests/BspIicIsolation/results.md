# BSP IIC 隔离测试

使用实际BSP/Framework代码和可控HAL测试桩；验证调用逻辑，不模拟I2C电气行为。

| 用例 | 结果 | 返回值 | 失败行 |
|---|---|---|---|
| 成功配置、硬件顺序及重复Init | Pass | 0 | 0 |
| 错误内核频率拒绝初始化 | Pass | -4 | 0 |
| RCC ERROR→IO及失败后重试 | Pass | -9 | 0 |
| RCC BUSY→BUSY及失败后重试 | Pass | -5 | 0 |
| RCC TIMEOUT→TIMEOUT及失败后重试 | Pass | -8 | 0 |
| HAL_I2C_Init ERROR→IO及失败后重试 | Pass | -9 | 0 |
| HAL_I2C_Init BUSY→BUSY及失败后重试 | Pass | -5 | 0 |
| HAL_I2C_Init TIMEOUT→TIMEOUT及失败后重试 | Pass | -8 | 0 |
| 模拟滤波 ERROR→IO及失败后重试 | Pass | -9 | 0 |
| 模拟滤波 BUSY→BUSY及失败后重试 | Pass | -5 | 0 |
| 模拟滤波 TIMEOUT→TIMEOUT及失败后重试 | Pass | -8 | 0 |
| 数字滤波 ERROR→IO及失败后重试 | Pass | -9 | 0 |
| 数字滤波 BUSY→BUSY及失败后重试 | Pass | -5 | 0 |
| 数字滤波 TIMEOUT→TIMEOUT及失败后重试 | Pass | -8 | 0 |
| 初始化重入返回BUSY | Pass | 0 | 0 |
| MSP忽略NULL和非I2C2 | Pass | 0 | 0 |
| 成功后频率变化拒绝复用Timing | Pass | 0 | 0 |
| MSP DeInit释放管脚和I2C2时钟 | Pass | 0 | 0 |
| BUS注册项 | Pass | 0 | 0 |
| BUS成功后才执行DEVICE | Pass | 0 | 0 |
| BUS失败中止DEVICE | Pass | -9 | 0 |

21/21 Pass。
成功路径检查RCC选择→GPIOB时钟→AF4开漏→I2C2时钟→HAL初始化→模拟滤波→数字滤波。
