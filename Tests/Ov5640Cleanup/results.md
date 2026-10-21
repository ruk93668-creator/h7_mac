# OV5640最小迁移模拟测试

当前真实BSP/Device和清理后Application；测试主动调用Device Ops，不在正式工程恢复周期探测。GPIO模型逐位解码软件SCCB。

| 测试 | 实际结果 | 判定 |
|---|---|---|
| Device主动读取0x5640连续10次 | 次数10，成功10，失败0，done=0，Timer堆=0，发送字节80，断言120 | PASS |
| 写地址NACK连续10次 | 次数10，成功0，失败10，done=0，Timer堆=0，发送字节10，断言26 | PASS |
| 错误ID 0x1234 | 次数10，成功0，失败10，done=0，Timer堆=0，发送字节80，断言35 | PASS |
| 前2次失败后8次成功 | 次数10，成功8，失败2，done=0，Timer堆=0，发送字节66，断言35 | PASS |
| SDA拉低总线忙 | 次数10，成功0，失败10，done=0，Timer堆=0，发送字节0，断言26 | PASS |
| PLL1Q错误时拒绝解除复位 | 次数0，成功0，失败0，done=0，Timer堆=0，发送字节0，断言3 | PASS |
| DWT停滞拒绝初始化 | 次数0，成功0，失败0，done=0，Timer堆=0，发送字节0，断言3 | PASS |
| Device参数检查 | 次数0，成功0，失败0，done=0，Timer堆=0，发送字节0，断言15 | PASS |
| 寄存器高字节NACK | 次数10，成功0，失败10，done=0，Timer堆=0，发送字节20，断言26 | PASS |
| 寄存器低字节NACK | 次数10，成功0，失败10，done=0，Timer堆=0，发送字节30，断言26 | PASS |
| 读地址NACK | 次数10，成功0，失败10，done=0，Timer堆=0，发送字节40，断言26 | PASS |
| 正式运行20秒无Timer无自动ID读取 | 次数0，成功0，失败0，done=0，Timer堆=0，发送字节0，断言16 | PASS |
| XCLK/复位/10+20ms顺序 | 次数0，成功0，失败0，done=0，Timer堆=0，发送字节0，断言12 | PASS |
| 初始化前SCCB拒绝 | 次数0，成功0，失败0，done=0，Timer堆=0，发送字节0，断言2 | PASS |
| DWT计数回绕仍可读取 | 次数10，成功10，失败0，done=0，Timer堆=0，发送字节80，断言120 | PASS |
| 空Application初始化成功且不分配资源 | 次数0，成功0，失败0，done=0，Timer堆=0，发送字节0，断言4 | PASS |
| SCL拉低总线忙 | 次数10，成功0，失败10，done=0，Timer堆=0，发送字节0，断言26 | PASS |

{'total': 17, 'passed': 17, 'failed': 0, 'assertions': 521}

仅为模拟。未连接或测量真实摄像头供电、XCLK及芯片ID。GPIO模型不模拟模拟电气边沿、外部上拉和时钟延展。
