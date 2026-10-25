# USB Device第一阶段模拟测试

直接编译正式BSP、Platform及ST Device Core；HAL PCD、时钟、GPIO使用可控桩。Unicorn执行ARMCC5生成的ARM机器码。

| 用例 | 输入 | 预期 | 实际 | 结果 |
|---|---|---|---|---|
| 未初始化接口 / FS | 未初始化接口 | 断言通过 | {'fail_line': 0, 'checks': 3, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 0} | PASS |
| 硬件时钟GPIO配置保持PLL / FS | 硬件时钟GPIO配置保持PLL | 断言通过 | {'fail_line': 0, 'checks': 20, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 1} | PASS |
| HSI48失败 / FS | HSI48失败 | 断言通过 | {'fail_line': 0, 'checks': 3, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 2} | PASS |
| USB时钟选择失败 / FS | USB时钟选择失败 | 断言通过 | {'fail_line': 0, 'checks': 3, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 3} | PASS |
| 时钟频率错误 / FS | 时钟频率错误 | 断言通过 | {'fail_line': 0, 'checks': 3, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 4} | PASS |
| PCD初始化失败 / FS | PCD初始化失败 | 断言通过 | {'fail_line': 0, 'checks': 3, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 5} | PASS |
| Rx FIFO失败 / FS | Rx FIFO失败 | 断言通过 | {'fail_line': 0, 'checks': 3, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 6} | PASS |
| Tx FIFO失败 / FS | Tx FIFO失败 | 断言通过 | {'fail_line': 0, 'checks': 3, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 7} | PASS |
| 重复初始化 / FS | 重复初始化 | 断言通过 | {'fail_line': 0, 'checks': 7, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 8} | PASS |
| 启动与停止NVIC / FS | 启动与停止NVIC | 断言通过 | {'fail_line': 0, 'checks': 9, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 9} | PASS |
| 启动HAL_BUSY / FS | 启动HAL_BUSY | 断言通过 | {'fail_line': 0, 'checks': 7, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 10} | PASS |
| Stop底层错误保留 / FS | Stop底层错误保留 | 断言通过 | {'fail_line': 0, 'checks': 9, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 11} | PASS |
| Reset打开EP0 / FS | Reset打开EP0 | 断言通过 | {'fail_line': 0, 'checks': 9, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 12} | PASS |
| 完整设备描述符 / FS | 完整设备描述符 | 断言通过 | {'fail_line': 0, 'checks': 11, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 13} | PASS |
| 设备描述符前8字节 / FS | 设备描述符前8字节 | 断言通过 | {'fail_line': 0, 'checks': 11, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 14} | PASS |
| 配置描述符头 / FS | 配置描述符头 | 断言通过 | {'fail_line': 0, 'checks': 9, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 15} | PASS |
| 完整配置描述符 / FS | 完整配置描述符 | 断言通过 | {'fail_line': 0, 'checks': 11, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 16} | PASS |
| 语言描述符 / FS | 语言描述符 | 断言通过 | {'fail_line': 0, 'checks': 9, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 17} | PASS |
| 厂家字符串 / FS | 厂家字符串 | 断言通过 | {'fail_line': 0, 'checks': 9, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 18} | PASS |
| 产品字符串 / FS | 产品字符串 | 断言通过 | {'fail_line': 0, 'checks': 9, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 19} | PASS |
| 序列号字符串 / FS | 序列号字符串 | 断言通过 | {'fail_line': 0, 'checks': 9, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 20} | PASS |
| 设置地址 / FS | 设置地址 | 断言通过 | {'fail_line': 0, 'checks': 9, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 21} | PASS |
| 非法地址STALL / FS | 非法地址STALL | 断言通过 | {'fail_line': 0, 'checks': 8, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 22} | PASS |
| 未寻址禁止配置 / FS | 未寻址禁止配置 | 断言通过 | {'fail_line': 0, 'checks': 8, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 23} | PASS |
| 配置完成 / FS | 配置完成 | 断言通过 | {'fail_line': 0, 'checks': 8, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 24} | PASS |
| 取消配置 / FS | 取消配置 | 断言通过 | {'fail_line': 0, 'checks': 9, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 25} | PASS |
| GET_CONFIGURATION / FS | GET_CONFIGURATION | 断言通过 | {'fail_line': 0, 'checks': 9, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 26} | PASS |
| GET_STATUS / FS | GET_STATUS | 断言通过 | {'fail_line': 0, 'checks': 9, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 27} | PASS |
| 未知描述符STALL / FS | 未知描述符STALL | 断言通过 | {'fail_line': 0, 'checks': 8, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 28} | PASS |
| 厂商请求STALL / FS | 厂商请求STALL | 断言通过 | {'fail_line': 0, 'checks': 8, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 29} | PASS |
| UVC Probe不支持 / FS | UVC Probe不支持 | 断言通过 | {'fail_line': 0, 'checks': 8, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 30} | PASS |
| SET_INTERFACE视频切换不支持 / FS | SET_INTERFACE视频切换不支持 | 断言通过 | {'fail_line': 0, 'checks': 8, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 31} | PASS |
| GET_INTERFACE零 / FS | GET_INTERFACE零 | 断言通过 | {'fail_line': 0, 'checks': 9, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 32} | PASS |
| 挂起与恢复 / FS | 挂起与恢复 | 断言通过 | {'fail_line': 0, 'checks': 9, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 33} | PASS |
| Reset清除配置 / FS | Reset清除配置 | 断言通过 | {'fail_line': 0, 'checks': 9, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 34} | PASS |
| 停止重新枚举 / FS | 停止重新枚举 | 断言通过 | {'fail_line': 0, 'checks': 12, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 35} | PASS |
| DeInit关闭资源 / FS | DeInit关闭资源 | 断言通过 | {'fail_line': 0, 'checks': 11, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 36} | PASS |
| 非法Class注册 / FS | 非法Class注册 | 断言通过 | {'fail_line': 0, 'checks': 6, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 37} | PASS |
| 运行中禁止注册 / FS | 运行中禁止注册 | 断言通过 | {'fail_line': 0, 'checks': 8, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 38} | PASS |
| 停止时Class注册 / FS | 停止时Class注册 | 断言通过 | {'fail_line': 0, 'checks': 5, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 39} | PASS |
| 固定内存分配 / FS | 固定内存分配 | 断言通过 | {'fail_line': 0, 'checks': 8, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 40} | PASS |
| 非零端点未启用 / FS | 非零端点未启用 | 断言通过 | {'fail_line': 0, 'checks': 8, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 41} | PASS |
| LL错误传播 / FS | LL错误传播 | 断言通过 | {'fail_line': 0, 'checks': 10, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 42} | PASS |
| 其他PCD回调隔离 / FS | 其他PCD回调隔离 | 断言通过 | {'fail_line': 0, 'checks': 8, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 43} | PASS |
| IRQ调用PCD / FS | IRQ调用PCD | 断言通过 | {'fail_line': 0, 'checks': 8, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 44} | PASS |
| 仅FS拒绝Qualifier / FS | 仅FS拒绝Qualifier | 断言通过 | {'fail_line': 0, 'checks': 8, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 45} | PASS |
| STALL清除 / FS | STALL清除 | 断言通过 | {'fail_line': 0, 'checks': 11, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 46} | PASS |
| EP0数据及状态阶段回调 / FS | EP0数据及状态阶段回调 | 断言通过 | {'fail_line': 0, 'checks': 10, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 47} | PASS |
| 配置描述符长度截断 / FS | 配置描述符长度截断 | 断言通过 | {'fail_line': 0, 'checks': 8, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 48} | PASS |
| 非法配置值STALL / FS | 非法配置值STALL | 断言通过 | {'fail_line': 0, 'checks': 8, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 49} | PASS |
| Class字符串回调完整性 / FS | Class字符串回调完整性 | 断言通过 | {'fail_line': 0, 'checks': 9, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 50} | PASS |
| 重复Class注册数量 / FS | 重复Class注册数量 | 断言通过 | {'fail_line': 0, 'checks': 11, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 51} | PASS |

{'total': 52, 'passed': 52, 'failed': 0, 'assertions': 428}

正式源文件变化：[]

限制：不连接USB主机，不验证物理48MHz精度、USB电气信号和Windows枚举；HAL硬件函数在本套中使用桩，真实HAL仅由正式Target编译验收。
