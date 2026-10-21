# V0 SDRAM 隔离模拟验收

真实 main MPU函数、BSP、Diagnostics 和 Framework Initcall；HAL接口及寄存器状态由Mock提供。

| 测试输入 | 预期 | 实际 | 结果 |
|---|---|---|---|
| 正常MPU/110MHz/时序/命令/1KB读写恢复 | 初始化0，SDCLK计算110MHz，验收0/状态2；恢复原数据，仅访问首1KB | Initcall=0，检查=0/状态2，命令4，校验1792字，恢复=True，越界=False，断言325 | PASS |
| PLL2已被占用 | 返回对应BUSY/INVALID_STATE/IO/TIMEOUT，后续命令及诊断停止 | Initcall=-5，检查=-4/状态0，命令0，校验0字，恢复=True，越界=False，断言8 | PASS |
| PLL源不是HSI | 返回对应BUSY/INVALID_STATE/IO/TIMEOUT，后续命令及诊断停止 | Initcall=-4，检查=-4/状态0，命令0，校验0字，恢复=True，越界=False，断言8 | PASS |
| HSI分频不符 | 返回对应BUSY/INVALID_STATE/IO/TIMEOUT，后续命令及诊断停止 | Initcall=-4，检查=-4/状态0，命令0，校验0字，恢复=True，越界=False，断言8 | PASS |
| HSI未就绪 | 返回对应BUSY/INVALID_STATE/IO/TIMEOUT，后续命令及诊断停止 | Initcall=-4，检查=-4/状态0，命令0，校验0字，恢复=True，越界=False，断言8 | PASS |
| PLL2 HAL_ERROR | 返回对应BUSY/INVALID_STATE/IO/TIMEOUT，后续命令及诊断停止 | Initcall=-9，检查=-4/状态0，命令0，校验0字，恢复=True，越界=False，断言8 | PASS |
| PLL2 HAL_TIMEOUT | 返回对应BUSY/INVALID_STATE/IO/TIMEOUT，后续命令及诊断停止 | Initcall=-8，检查=-4/状态0，命令0，校验0字，恢复=True，越界=False，断言8 | PASS |
| FMC频率错误 | 返回对应BUSY/INVALID_STATE/IO/TIMEOUT，后续命令及诊断停止 | Initcall=-4，检查=-4/状态0，命令0，校验0字，恢复=True，越界=False，断言7 | PASS |
| HAL_SDRAM_Init失败 | 返回对应BUSY/INVALID_STATE/IO/TIMEOUT，后续命令及诊断停止 | Initcall=-9，检查=-4/状态0，命令0，校验0字，恢复=True，越界=False，断言7 | PASS |
| HAL_SDRAM_Init超时 | 返回对应BUSY/INVALID_STATE/IO/TIMEOUT，后续命令及诊断停止 | Initcall=-8，检查=-4/状态0，命令0，校验0字，恢复=True，越界=False，断言7 | PASS |
| MSP缺失 | 返回对应BUSY/INVALID_STATE/IO/TIMEOUT，后续命令及诊断停止 | Initcall=-4，检查=-4/状态0，命令0，校验0字，恢复=True，越界=False，断言7 | PASS |
| 时钟命令失败 | 返回对应BUSY/INVALID_STATE/IO/TIMEOUT，后续命令及诊断停止 | Initcall=-9，检查=-4/状态0，命令1，校验0字，恢复=True，越界=False，断言8 | PASS |
| 预充电命令失败 | 返回对应BUSY/INVALID_STATE/IO/TIMEOUT，后续命令及诊断停止 | Initcall=-9，检查=-4/状态0，命令2，校验0字，恢复=True，越界=False，断言8 | PASS |
| 自动刷新命令失败 | 返回对应BUSY/INVALID_STATE/IO/TIMEOUT，后续命令及诊断停止 | Initcall=-9，检查=-4/状态0，命令3，校验0字，恢复=True，越界=False，断言8 | PASS |
| 模式命令失败 | 返回对应BUSY/INVALID_STATE/IO/TIMEOUT，后续命令及诊断停止 | Initcall=-9，检查=-4/状态0，命令4，校验0字，恢复=True，越界=False，断言8 | PASS |
| 命令超时 | 返回对应BUSY/INVALID_STATE/IO/TIMEOUT，后续命令及诊断停止 | Initcall=-8，检查=-4/状态0，命令2，校验0字，恢复=True，越界=False，断言7 | PASS |
| 刷新计数失败 | 返回对应BUSY/INVALID_STATE/IO/TIMEOUT，后续命令及诊断停止 | Initcall=-9，检查=-4/状态0，命令4，校验0字，恢复=True，越界=False，断言7 | PASS |
| 刷新计数超时 | 返回对应BUSY/INVALID_STATE/IO/TIMEOUT，后续命令及诊断停止 | Initcall=-8，检查=-4/状态0，命令4，校验0字，恢复=True，越界=False，断言7 | PASS |
| 重复初始化和重复验收 | 初始化0，SDCLK计算110MHz，验收0/状态2；恢复原数据，仅访问首1KB | Initcall=0，检查=0/状态2，命令4，校验1792字，恢复=True，越界=False，断言582 | PASS |
| 未初始化拒绝读写验收 | 返回INVALID_STATE，不访问SDRAM | Initcall=0，检查=-4/状态0，命令0，校验0字，恢复=True，越界=False，断言2 | PASS |
| 图案读回错误并恢复 | 初始化BSP成功；诊断返回IO/状态3，定位0xD0000000；保留首错 | Initcall=-9，检查=-9/状态3，命令4，校验257字，恢复=True，越界=False，断言8 | PASS |
| 恢复数据读回错误 | 初始化BSP成功；诊断返回IO/状态3，定位0xD0000000；保留首错 | Initcall=-9，检查=-9/状态3，命令4，校验1792字，恢复=False，越界=False，断言8 | PASS |
| 验收重入BUSY | 返回BUSY，不访问SDRAM | Initcall=0，检查=-4/状态1，命令0，校验0字，恢复=True，越界=False，断言2 | PASS |

{'total': 23, 'passed': 23, 'failed': 0, 'assertions': 1056}

恢复失败注入用例预期为检测出恢复错误，不要求错误硬件模型中的数据恢复成功。模拟不证明真实硬件已可用。
