# CameraStream隔离状态机测试

真实camera_capture.c直接编译到测试翻译单元；不链接HAL。测试在Unicorn执行ARMCC5生成的ARM机器码。

| 用例 | 输入 | 预期 | 实际 | 结果 |
|---|---|---|---|---|
| 结构与初始化 / Y8 | 结构与初始化 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 9, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 0} | PASS |
| 结构与初始化 / RGB565 | 结构与初始化 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 9, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 1} | PASS |
| 结构与初始化 / YUV422 | 结构与初始化 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 9, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 2} | PASS |
| Snapshot释放后启动Stream / Y8 | Snapshot释放后启动Stream | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 16, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 3} | PASS |
| Snapshot释放后启动Stream / RGB565 | Snapshot释放后启动Stream | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 16, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 4} | PASS |
| Snapshot释放后启动Stream / YUV422 | Snapshot释放后启动Stream | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 16, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 5} | PASS |
| Stream停止后启动Snapshot / Y8 | Stream停止后启动Snapshot | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 18, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 6} | PASS |
| Stream停止后启动Snapshot / RGB565 | Stream停止后启动Snapshot | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 18, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 7} | PASS |
| Stream停止后启动Snapshot / YUV422 | Stream停止后启动Snapshot | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 18, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 8} | PASS |
| Snapshot停止后启动Stream / Y8 | Snapshot停止后启动Stream | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 13, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 9} | PASS |
| Snapshot停止后启动Stream / RGB565 | Snapshot停止后启动Stream | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 13, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 10} | PASS |
| Snapshot停止后启动Stream / YUV422 | Snapshot停止后启动Stream | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 13, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 11} | PASS |
| Stream启动IO错误 / Y8 | Stream启动IO错误 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 17, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 12} | PASS |
| Stream启动IO错误 / RGB565 | Stream启动IO错误 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 17, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 13} | PASS |
| Stream启动IO错误 / YUV422 | Stream启动IO错误 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 17, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 14} | PASS |
| Stop失败保留Stream与首次错误 / Y8 | Stop失败保留Stream与首次错误 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 21, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 15} | PASS |
| Stop失败保留Stream与首次错误 / RGB565 | Stop失败保留Stream与首次错误 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 21, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 16} | PASS |
| Stop失败保留Stream与首次错误 / YUV422 | Stop失败保留Stream与首次错误 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 21, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 17} | PASS |
| Stream不使用Snapshot超时 / Y8 | Stream不使用Snapshot超时 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 15, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 18} | PASS |
| Stream不使用Snapshot超时 / RGB565 | Stream不使用Snapshot超时 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 15, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 19} | PASS |
| Stream不使用Snapshot超时 / YUV422 | Stream不使用Snapshot超时 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 15, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 20} | PASS |
| Stream不处理Snapshot FRAME / Y8 | Stream不处理Snapshot FRAME | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 16, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 21} | PASS |
| Stream不处理Snapshot FRAME / RGB565 | Stream不处理Snapshot FRAME | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 16, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 22} | PASS |
| Stream不处理Snapshot FRAME / YUV422 | Stream不处理Snapshot FRAME | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 16, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 23} | PASS |
| Stream错误恢复 / Y8 | Stream错误恢复 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 18, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 24} | PASS |
| Stream错误恢复 / RGB565 | Stream错误恢复 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 18, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 25} | PASS |
| Stream错误恢复 / YUV422 | Stream错误恢复 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 18, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 26} | PASS |
| Stream错误且恢复重试 / Y8 | Stream错误且恢复重试 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 21, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 27} | PASS |
| Stream错误且恢复重试 / RGB565 | Stream错误且恢复重试 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 21, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 28} | PASS |
| Stream错误且恢复重试 / YUV422 | Stream错误且恢复重试 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 21, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 29} | PASS |
| 嵌套回调上下文验证 / Y8 | 嵌套回调上下文验证 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 19, 'operations': 0, 'replaces': 1, 'deferred': 0, 'case_id': 30} | PASS |
| 嵌套回调上下文验证 / RGB565 | 嵌套回调上下文验证 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 19, 'operations': 0, 'replaces': 1, 'deferred': 0, 'case_id': 31} | PASS |
| 嵌套回调上下文验证 / YUV422 | 嵌套回调上下文验证 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 19, 'operations': 0, 'replaces': 1, 'deferred': 0, 'case_id': 32} | PASS |
| Snapshot完整Get Release及再次启动 / Y8 | Snapshot完整Get Release及再次启动 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 21, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 33} | PASS |
| Snapshot完整Get Release及再次启动 / RGB565 | Snapshot完整Get Release及再次启动 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 21, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 34} | PASS |
| Snapshot完整Get Release及再次启动 / YUV422 | Snapshot完整Get Release及再次启动 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 21, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 35} | PASS |
| Snapshot失败恢复保留模式 / Y8 | Snapshot失败恢复保留模式 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 16, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 36} | PASS |
| Snapshot失败恢复保留模式 / RGB565 | Snapshot失败恢复保留模式 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 16, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 37} | PASS |
| Snapshot失败恢复保留模式 / YUV422 | Snapshot失败恢复保留模式 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 16, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 38} | PASS |
| READY拒绝Stream和Stop / Y8 | READY拒绝Stream和Stop | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 13, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 39} | PASS |
| READY拒绝Stream和Stop / RGB565 | READY拒绝Stream和Stop | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 13, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 40} | PASS |
| READY拒绝Stream和Stop / YUV422 | READY拒绝Stream和Stop | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 13, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 41} | PASS |
| Stream拒绝Snapshot及重复启动 / Y8 | Stream拒绝Snapshot及重复启动 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 17, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 42} | PASS |
| Stream拒绝Snapshot及重复启动 / RGB565 | Stream拒绝Snapshot及重复启动 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 17, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 43} | PASS |
| Stream拒绝Snapshot及重复启动 / YUV422 | Stream拒绝Snapshot及重复启动 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 17, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 44} | PASS |
| 空闲Stop无硬件调用 / Y8 | 空闲Stop无硬件调用 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 12, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 45} | PASS |
| 空闲Stop无硬件调用 / RGB565 | 空闲Stop无硬件调用 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 12, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 46} | PASS |
| 空闲Stop无硬件调用 / YUV422 | 空闲Stop无硬件调用 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 12, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 47} | PASS |
| 非法配置不初始化 / Y8 | 非法配置不初始化 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 3, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 48} | PASS |
| 非法配置不初始化 / RGB565 | 非法配置不初始化 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 3, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 49} | PASS |
| 非法配置不初始化 / YUV422 | 非法配置不初始化 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 3, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 50} | PASS |
| Stream初始化失败不占模式 / Y8 | Stream初始化失败不占模式 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 12, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 51} | PASS |
| Stream初始化失败不占模式 / RGB565 | Stream初始化失败不占模式 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 12, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 52} | PASS |
| Stream初始化失败不占模式 / YUV422 | Stream初始化失败不占模式 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 12, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 53} | PASS |
| Stream启动BUSY恢复 / Y8 | Stream启动BUSY恢复 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 17, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 54} | PASS |
| Stream启动BUSY恢复 / RGB565 | Stream启动BUSY恢复 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 17, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 55} | PASS |
| Stream启动BUSY恢复 / YUV422 | Stream启动BUSY恢复 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 17, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 56} | PASS |
| Stream启动TIMEOUT恢复 / Y8 | Stream启动TIMEOUT恢复 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 17, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 57} | PASS |
| Stream启动TIMEOUT恢复 / RGB565 | Stream启动TIMEOUT恢复 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 17, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 58} | PASS |
| Stream启动TIMEOUT恢复 / YUV422 | Stream启动TIMEOUT恢复 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 17, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 59} | PASS |
| Rotate失败不提交对象 / Y8 | Rotate失败不提交对象 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 15, 'operations': 0, 'replaces': 1, 'deferred': 0, 'case_id': 60} | PASS |
| Rotate失败不提交对象 / RGB565 | Rotate失败不提交对象 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 15, 'operations': 0, 'replaces': 1, 'deferred': 0, 'case_id': 61} | PASS |
| Rotate失败不提交对象 / YUV422 | Rotate失败不提交对象 | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 15, 'operations': 0, 'replaces': 1, 'deferred': 0, 'case_id': 62} | PASS |
| Snapshot Tick回绕99ms / Y8 | Snapshot Tick回绕99ms | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 10, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 63} | PASS |
| Snapshot Tick回绕99ms / RGB565 | Snapshot Tick回绕99ms | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 10, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 64} | PASS |
| Snapshot Tick回绕99ms / YUV422 | Snapshot Tick回绕99ms | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 10, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 65} | PASS |
| Snapshot Tick回绕100ms / Y8 | Snapshot Tick回绕100ms | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 12, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 66} | PASS |
| Snapshot Tick回绕100ms / RGB565 | Snapshot Tick回绕100ms | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 12, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 67} | PASS |
| Snapshot Tick回绕100ms / YUV422 | Snapshot Tick回绕100ms | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 12, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 68} | PASS |
| Stream临界区保持PRIMASK / Y8 | Stream临界区保持PRIMASK | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 17, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 69} | PASS |
| Stream临界区保持PRIMASK / RGB565 | Stream临界区保持PRIMASK | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 17, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 70} | PASS |
| Stream临界区保持PRIMASK / YUV422 | Stream临界区保持PRIMASK | 状态与返回值符合约定 | {'fail_line': 0, 'checks': 17, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 71} | PASS |

{'total': 72, 'passed': 72, 'failed': 0, 'assertions': 1095}

正式源文件变化：[]

限制：不验证真实DMA双缓冲CT/中断时序、BSP切换实现、传感器信号、MPU或像素格式正确性。模拟DMA写入仅为测试数据。
