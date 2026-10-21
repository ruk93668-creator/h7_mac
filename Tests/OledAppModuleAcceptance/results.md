# CM4 OLED Application集成模拟测试

真实Application、Timer、Event、Runtime、Device、OLED和Initcall源码；仅模拟BSP通信和Platform_TimeGetMs。

| 测试 | 输入 | 预期 | 实际 | 结果 |
|---|---|---|---|---|
| 初始化和中间横线 | 真实Initcall、Device、OLED和应用初始化 | APPLICATION创建1个100ms周期Timer；仅y=16的128个像素置位 | 事务0笔；Timer池/堆=1/1；刷新/轮询结果=0/0；断言267项 | FAIL |
| 重复初始化 | 两次App_Init | 不重复分配Timer | 事务0笔；Timer池/堆=1/1；刷新/轮询结果=0/0；断言14项 | PASS |
| 初始化前运行 | App_Run | 安全返回 | 事务0笔；Timer池/堆=0/0；刷新/轮询结果=0/0；断言3项 | PASS |
| 100ms边界 | 99/100/199/200ms及逐笔完成 | 100、200ms触发，两帧共16笔事务 | 事务16笔；Timer池/堆=1/1；刷新/轮询结果=0/0；断言1151项 | PASS |
| 忙时跳过 | 第一帧未结束时推进200/300ms | 不重复提交，不记录BUSY为错误，完成后可继续 | 事务9笔；Timer池/堆=1/1；刷新/轮询结果=0/0；断言587项 | PASS |
| 错误记录 | 第一笔完成返回IO后再到下周期 | 记录-9，OLED回IDLE，下周期重新启动，保留错误记录 | 事务2笔；Timer池/堆=1/1；刷新/轮询结果=-9/-9；断言17项 | PASS |
| Timer池满 | 16个Timer占满后初始化 | 返回NO_RESOURCE，无应用运行指针 | 事务0笔；Timer池/堆=16/0；刷新/轮询结果=0/0；断言20项 | PASS |
| 设备未READY | 只注册后初始化，再完成Device_InitAll后重试 | 先返回INVALID_STATE，后成功 | 事务0笔；Timer池/堆=1/1；刷新/轮询结果=0/0；断言5项 | PASS |
| 设备不存在 | Registry为空 | 返回NOT_FOUND | 事务0笔；Timer池/堆=0/0；刷新/轮询结果=0/0；断言2项 | PASS |
| Ops缺失 | OLED Ops=NULL | 返回NOT_SUPPORTED，不创建Timer | 事务0笔；Timer池/堆=0/0；刷新/轮询结果=0/0；断言3项 | PASS |
| 连续两帧横线字节 | 两次刷新并逐字节核对 | CMD0/DATA0至CMD3/DATA3，横线图案一致，无Page4 | 事务16笔；Timer池/堆=1/1；刷新/轮询结果=0/0；断言1151项 | PASS |
| App_Run持续轮询 | 等待命令时连续20次App_Run，然后完成命令 | 20次TakeResult，完成后推进DATA0 | 事务2笔；Timer池/堆=1/1；刷新/轮询结果=0/0；断言17项 | PASS |

{'total': 12, 'passed': 11, 'failed': 1, 'assertions': 3237}

不执行真实I2C电气行为或真实BSP/HAL，不能代替上板测试。Application通过真实OLED Ops清屏并绘制y=16整条横线，测试核对真实帧缓冲与发送字节。
