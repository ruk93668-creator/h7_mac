# CM7 DCMI+DMA 初始化模拟验收

编译真实BSP、Framework和HAL GPIO/DMA/DMAEx/DCMI驱动；寄存器使用Unicorn内存模型，NVIC为调用记录桩。

| 测试输入 | 预期 | 实际 | 结果 |
|---|---|---|---|
| 真实HAL初始化/AF13/DMA/DCMI/NVIC | 初始化与寄存器配置正确，无采集/传输启动，无SDRAM访问 | 返回0，DMA/DCMI初始化1/1次，NVIC掩码3，DMA/DCMI CR=0x25400/0xE0，自动启动=False，SDRAM访问=0，断言125 | PASS |
| 重复EarlyInit和Init幂等 | 初始化与寄存器配置正确，无采集/传输启动，无SDRAM访问 | 返回0，DMA/DCMI初始化1/1次，NVIC掩码3，DMA/DCMI CR=0x25400/0xE0，自动启动=False，SDRAM访问=0，断言130 | PASS |
| 直接Init补齐引脚 | 初始化与寄存器配置正确，无采集/传输启动，无SDRAM访问 | 返回0，DMA/DCMI初始化1/1次，NVIC掩码3，DMA/DCMI CR=0x25400/0xE0，自动启动=False，SDRAM访问=0，断言125 | PASS |
| DMA Stream运行中拒绝 | BUSY，NVIC不开放，不启动采集 | 返回-5，DMA/DCMI初始化0/0次，NVIC掩码0，DMA/DCMI CR=0x1/0x0，自动启动=False，SDRAM访问=0，断言5 | PASS |
| DCMI已使能拒绝 | BUSY，NVIC不开放，不启动采集 | 返回-5，DMA/DCMI初始化0/0次，NVIC掩码0，DMA/DCMI CR=0x0/0x4000，自动启动=False，SDRAM访问=0，断言5 | PASS |
| DCMI Capture运行中拒绝 | BUSY，NVIC不开放，不启动采集 | 返回-5，DMA/DCMI初始化0/0次，NVIC掩码0，DMA/DCMI CR=0x0/0x1，自动启动=False，SDRAM访问=0，断言5 | PASS |
| DMA HAL_ERROR | IO，按阶段清理DMA，不开放NVIC | 返回-9，DMA/DCMI初始化1/0次，NVIC掩码0，DMA/DCMI CR=0x0/0x0，自动启动=False，SDRAM访问=0，断言4 | PASS |
| DMA HAL_BUSY | BUSY，NVIC不开放，不启动采集 | 返回-5，DMA/DCMI初始化1/0次，NVIC掩码0，DMA/DCMI CR=0x0/0x0，自动启动=False，SDRAM访问=0，断言4 | PASS |
| DMA HAL_TIMEOUT | TIMEOUT，NVIC不开放，不启动采集 | 返回-8，DMA/DCMI初始化1/0次，NVIC掩码0，DMA/DCMI CR=0x0/0x0，自动启动=False，SDRAM访问=0，断言4 | PASS |
| DCMI HAL_ERROR及DMA清理 | IO，按阶段清理DMA，不开放NVIC | 返回-9，DMA/DCMI初始化1/1次，NVIC掩码0，DMA/DCMI CR=0x0/0x0，自动启动=False，SDRAM访问=0，断言4 | PASS |
| DCMI HAL_BUSY | BUSY，NVIC不开放，不启动采集 | 返回-5，DMA/DCMI初始化1/1次，NVIC掩码0，DMA/DCMI CR=0x0/0x0，自动启动=False，SDRAM访问=0，断言4 | PASS |
| DCMI HAL_TIMEOUT | TIMEOUT，NVIC不开放，不启动采集 | 返回-8，DMA/DCMI初始化1/1次，NVIC掩码0，DMA/DCMI CR=0x0/0x0，自动启动=False，SDRAM访问=0，断言4 | PASS |
| 失败后DMA清理失败 | IO，按阶段清理DMA，不开放NVIC | 返回-9，DMA/DCMI初始化1/1次，NVIC掩码0，DMA/DCMI CR=0x25400/0x0，自动启动=False，SDRAM访问=0，断言4 | PASS |
| DMA失败后重试 | 首次失败后可重试成功 | 返回-9，DMA/DCMI初始化2/1次，NVIC掩码3，DMA/DCMI CR=0x25400/0xE0，自动启动=False，SDRAM访问=0，断言127 | PASS |
| DCMI失败后重试 | 首次失败后可重试成功 | 返回-9，DMA/DCMI初始化2/2次，NVIC掩码3，DMA/DCMI CR=0x25400/0xE0，自动启动=False，SDRAM访问=0，断言127 | PASS |
| 真实DMA/DCMI IRQ路由 | 真实HAL IRQ分别回调正确的私有句柄，Parent关联正确 | 返回0，DMA/DCMI初始化1/1次，NVIC掩码3，DMA/DCMI CR=0x25400/0xE0，自动启动=False，SDRAM访问=0，断言129 | PASS |
| 非DCMI MSP不操作硬件 | 其他实例不进行GPIO/时钟初始化 | 返回0，DMA/DCMI初始化0/0次，NVIC掩码0，DMA/DCMI CR=0x0/0x0，自动启动=False，SDRAM访问=0，断言2 | PASS |
| BUS Initcall/无采集无Framebuffer | 初始化与寄存器配置正确，无采集/传输启动，无SDRAM访问 | 返回0，DMA/DCMI初始化1/1次，NVIC掩码3，DMA/DCMI CR=0x25400/0xE0，自动启动=False，SDRAM访问=0，断言125 | PASS |
| 旧NVIC挂起先清理 | 初始化与寄存器配置正确，无采集/传输启动，无SDRAM访问 | 返回0，DMA/DCMI初始化1/1次，NVIC掩码3，DMA/DCMI CR=0x25400/0xE0，自动启动=False，SDRAM访问=0，断言125 | PASS |
| 不改变SDRAM/SCCB/I2C引脚和PLL | 初始化与寄存器配置正确，无采集/传输启动，无SDRAM访问 | 返回0，DMA/DCMI初始化1/1次，NVIC掩码3，DMA/DCMI CR=0x25400/0xE0，自动启动=False，SDRAM访问=0，断言125 | PASS |

{'total': 20, 'passed': 20, 'failed': 0, 'assertions': 1183}

Busy用例中的EN/CAPTURE位由测试预先设置，用于模拟其他活动占用；BSP拒绝重新初始化。IRQ用例仅注入状态标志，不启动DMA。
