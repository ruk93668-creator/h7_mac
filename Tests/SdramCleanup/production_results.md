# 正式初始化无自动诊断

| 输入 | 预期 | 实际 | 结果 |
|---|---|---|---|
| 正式初始化不读写SDRAM、MPU/时钟/引脚/时序正确 | 匹配预期返回码和初始化顺序，SDRAM数据区零读写 | Initcall=0；检查=0；数据区读/写=0/0；越界=False；断言168 | PASS |
| PLL2占用 | 匹配预期返回码和初始化顺序，SDRAM数据区零读写 | Initcall=-5；检查=0；数据区读/写=0/0；越界=False；断言11 | PASS |
| PLL源错误 | 匹配预期返回码和初始化顺序，SDRAM数据区零读写 | Initcall=-4；检查=0；数据区读/写=0/0；越界=False；断言11 | PASS |
| HSI分频错误 | 匹配预期返回码和初始化顺序，SDRAM数据区零读写 | Initcall=-4；检查=0；数据区读/写=0/0；越界=False；断言11 | PASS |
| HSI未就绪 | 匹配预期返回码和初始化顺序，SDRAM数据区零读写 | Initcall=-4；检查=0；数据区读/写=0/0；越界=False；断言11 | PASS |
| PLL配置IO | 匹配预期返回码和初始化顺序，SDRAM数据区零读写 | Initcall=-9；检查=0；数据区读/写=0/0；越界=False；断言11 | PASS |
| PLL配置超时 | 匹配预期返回码和初始化顺序，SDRAM数据区零读写 | Initcall=-8；检查=0；数据区读/写=0/0；越界=False；断言11 | PASS |
| PLL2R频率不符 | 匹配预期返回码和初始化顺序，SDRAM数据区零读写 | Initcall=-4；检查=0；数据区读/写=0/0；越界=False；断言10 | PASS |
| HAL SDRAM初始化失败 | 匹配预期返回码和初始化顺序，SDRAM数据区零读写 | Initcall=-9；检查=0；数据区读/写=0/0；越界=False；断言10 | PASS |
| HAL SDRAM初始化超时 | 匹配预期返回码和初始化顺序，SDRAM数据区零读写 | Initcall=-8；检查=0；数据区读/写=0/0；越界=False；断言10 | PASS |
| MSP缺失 | 匹配预期返回码和初始化顺序，SDRAM数据区零读写 | Initcall=-4；检查=0；数据区读/写=0/0；越界=False；断言10 | PASS |
| CLK_ENABLE失败 | 匹配预期返回码和初始化顺序，SDRAM数据区零读写 | Initcall=-9；检查=0；数据区读/写=0/0；越界=False；断言11 | PASS |
| PALL失败 | 匹配预期返回码和初始化顺序，SDRAM数据区零读写 | Initcall=-9；检查=0；数据区读/写=0/0；越界=False；断言11 | PASS |
| AUTOREFRESH失败 | 匹配预期返回码和初始化顺序，SDRAM数据区零读写 | Initcall=-9；检查=0；数据区读/写=0/0；越界=False；断言11 | PASS |
| LOAD_MODE失败 | 匹配预期返回码和初始化顺序，SDRAM数据区零读写 | Initcall=-9；检查=0；数据区读/写=0/0；越界=False；断言11 | PASS |
| 命令超时 | 匹配预期返回码和初始化顺序，SDRAM数据区零读写 | Initcall=-8；检查=0；数据区读/写=0/0；越界=False；断言10 | PASS |
| 刷新设置失败 | 匹配预期返回码和初始化顺序，SDRAM数据区零读写 | Initcall=-9；检查=0；数据区读/写=0/0；越界=False；断言10 | PASS |
| 刷新设置超时 | 匹配预期返回码和初始化顺序，SDRAM数据区零读写 | Initcall=-8；检查=0；数据区读/写=0/0；越界=False；断言10 | PASS |
| 重复Framework初始化无测试无重复配置 | 匹配预期返回码和初始化顺序，SDRAM数据区零读写 | Initcall=0；检查=0；数据区读/写=0/0；越界=False；断言173 | PASS |
| FMC选择器不符 | 匹配预期返回码和初始化顺序，SDRAM数据区零读写 | Initcall=-4；检查=0；数据区读/写=0/0；越界=False；断言10 | PASS |

{'total': 20, 'passed': 20, 'failed': 0, 'assertions': 531}

仅为ARMCC/Unicorn及HAL Mock验收，不代替电气时序及真实存储器测试。
