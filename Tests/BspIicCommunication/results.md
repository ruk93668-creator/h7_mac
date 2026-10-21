# CM4 I2C2 同步/异步通信模拟测试

直接包含当前真实 CM4/BSP/bsp_iic.c；Mock HAL；ARMCC5 编译 ARM 可执行映像，Unicorn Cortex-M 模式实际执行。
普通中断在 PRIMASK=1 时只挂起，恢复 0 且 NVIC 使能后经真实 BSP IRQ 入口调用真实 BSP 回调。
每例从新的仿真内存开始，通过真实 BUS Initcall 初始化；断言与时间线详见 results.json。

| 编号 | 测试 | 输入 | 预期 | 实际 | 结果 |
|---|---|---|---|---|---|
| T01 | 异步正常启动 | 地址0x3C，长度2 | SYS_OK；TRANSFERRING；HAL地址0x78 | 断言14项；最终TRANSFERRING；HAL同步/异步/Abort=0/1/0；IRQ交付/屏蔽挂起=0/0；PRIMASK=0 | PASS |
| T02 | 正常完成 | 启动后投递Tx完成IRQ，再TakeResult | true、SYS_OK，进入IDLE | 断言14项；最终IDLE；HAL同步/异步/Abort=0/1/0；IRQ交付/屏蔽挂起=1/0；PRIMASK=0 | PASS |
| T03 | 结果只交付一次 | 正常完成后读取两次 | 第一次true、0，第二次false且输出不变 | 断言16项；最终IDLE；HAL同步/异步/Abort=0/1/0；IRQ交付/屏蔽挂起=1/0；PRIMASK=0 | PASS |
| T04 | 发送期间重入发送 | 异步未完成，分别同步和异步发送 | 均BUSY=-5，无新HAL发送 | 断言14项；最终TRANSFERRING；HAL同步/异步/Abort=0/1/0；IRQ交付/屏蔽挂起=0/0；PRIMASK=0 | PASS |
| T05 | 99ms未超时 | 启动后Tick增加99，再TakeResult | false，Abort次数0，保持TRANSFERRING | 断言14项；最终TRANSFERRING；HAL同步/异步/Abort=0/1/0；IRQ交付/屏蔽挂起=0/0；PRIMASK=0 | PASS |
| T06 | 100ms请求Abort | 启动后Tick增加100，再TakeResult | 调用一次Abort，地址0x78，保持ABORTING | 断言16项；最终ABORTING；HAL同步/异步/Abort=0/1/1；IRQ交付/屏蔽挂起=0/0；PRIMASK=0 | PASS |
| T07 | Abort成功及完成 | Abort内产生完成IRQ，PRIMASK屏蔽时挂起 | 恢复中断后完成，下次TakeResult为true、TIMEOUT=-8；只交付一次 | 断言21项；最终IDLE；HAL同步/异步/Abort=0/1/1；IRQ交付/屏蔽挂起=1/1；PRIMASK=0 | PASS |
| T08 | Abort启动失败 | 100ms后Abort返回HAL_ERROR | 立即交付true、IO=-9，进入FAULT | 断言18项；最终FAULT；HAL同步/异步/Abort=0/1/1；IRQ交付/屏蔽挂起=0/0；PRIMASK=0 | PASS |
| T09 | FAULT拒绝发送 | Abort失败进入FAULT，再同步和异步发送 | 均IO=-9，不复位为IDLE | 断言20项；最终FAULT；HAL同步/异步/Abort=0/1/1；IRQ交付/屏蔽挂起=0/0；PRIMASK=0 | PASS |
| T10 | 普通HAL错误 | 投递AF错误IRQ | true、IO=-9，进入IDLE | 断言15项；最终IDLE；HAL同步/异步/Abort=0/1/0；IRQ交付/屏蔽挂起=1/0；PRIMASK=0 | PASS |
| T11 | HAL超时错误 | 投递TIMEOUT / AF错误IRQ | true、TIMEOUT=-8 | 断言15项；最终IDLE；HAL同步/异步/Abort=0/1/0；IRQ交付/屏蔽挂起=1/0；PRIMASK=0 | PASS |
| T12 | 连续不同地址 | 0x08、0x3C、0x77逐个异步完成并同步发送 | HAL逐次收到0x10、0x78、0xEE | 断言26项；最终IDLE；HAL同步/异步/Abort=3/3/0；IRQ交付/屏蔽挂起=3/0；PRIMASK=0 | PASS |
| T13 | Abort使用当前地址 | 先完成0x3C，再启动0x55并超时 | Abort收到0xAA，而非前一事务地址 | 断言18项；最终ABORTING；HAL同步/异步/Abort=0/2/1；IRQ交付/屏蔽挂起=1/0；PRIMASK=0 | PASS |
| T14 | 129字节边界 | 异步129字节，启动后改写调用方缓冲区 | 接受，HAL副本及BSP内部副本保持全部原始129字节 | 断言272项；最终IDLE；HAL同步/异步/Abort=0/1/0；IRQ交付/屏蔽挂起=1/0；PRIMASK=0 | PASS |
| T15 | 130字节越界 | 异步130字节 | INVALID_ARG=-1，无HAL调用 | 断言10项；最终IDLE；HAL同步/异步/Abort=0/0/0；IRQ交付/屏蔽挂起=0/0；PRIMASK=0 | PASS |
| T16 | 非法参数矩阵 | NULL、零长度、所有非法uint8地址，同步及异步 | 均INVALID_ARG=-1，无HAL调用 | 断言302项；最终IDLE；HAL同步/异步/Abort=0/0/0；IRQ交付/屏蔽挂起=0/0；PRIMASK=0 | PASS |
| T17 | 完成与超时交错 | 100ms时在读取PRIMASK后、屏蔽中断前完成 | 不Abort、不覆盖COMPLETED，下次交付SYS_OK | 断言17项；最终IDLE；HAL同步/异步/Abort=0/1/0；IRQ交付/屏蔽挂起=1/0；PRIMASK=0 | PASS |
| T18 | 恢复初始PRIMASK | 分别以PRIMASK=1和0进入Abort，并产生完成IRQ | 准确恢复原值；mask=1时回调仍挂起，手动解屏蔽后交付超时 | 断言24项；最终IDLE；HAL同步/异步/Abort=0/2/2；IRQ交付/屏蔽挂起=2/2；PRIMASK=0 | PASS |
| T19 | HAL_BUSY启动失败可重试 | 启动返回HAL_BUSY，之后HAL_OK重试 | 先BUSY=-5并回IDLE，之后启动并完成成功 | 断言14项；最终IDLE；HAL同步/异步/Abort=0/2/0；IRQ交付/屏蔽挂起=1/0；PRIMASK=0 | PASS |
| T20 | Abort成功但无回调 | Abort返回HAL_OK，不投递回调，再轮询8秒 | 保持ABORTING；不重复Abort、不交付结果；后续发送BUSY | 断言35项；最终ABORTING；HAL同步/异步/Abort=0/1/1；IRQ交付/屏蔽挂起=0/0；PRIMASK=0 | PASS |
| E01 | 同步返回值映射 | HAL_OK/ERROR/BUSY/TIMEOUT同步发送 | 分别0/-9/-5/-8，100ms参数，BSP仍IDLE | 断言21项；最终IDLE；HAL同步/异步/Abort=4/0/0；IRQ交付/屏蔽挂起=0/0；PRIMASK=0 | PASS |
| E02 | HAL状态准入 | RESET、BUSY_TX状态分别请求同步/异步 | 同步RESET=-4；其他BUSY=-5；无HAL发送 | 断言14项；最终IDLE；HAL同步/异步/Abort=0/0/0；IRQ交付/屏蔽挂起=0/0；PRIMASK=0 | PASS |
| E03 | NVIC反初始化 | mask=1挂起完成IRQ，然后MSP DeInit，再解屏蔽 | EV/ER均禁用，Pending清除，不投递回调 | 断言13项；最终IDLE；HAL同步/异步/Abort=0/0/0；IRQ交付/屏蔽挂起=0/1；PRIMASK=0 | PASS |
| E04 | Tick 32位回绕 | 起点0xFFFFFFF0，+99、+100 | 99ms不Abort；100ms Abort；回调交付TIMEOUT | 断言19项；最终IDLE；HAL同步/异步/Abort=0/1/1；IRQ交付/屏蔽挂起=1/0；PRIMASK=0 | PASS |
| E05 | PRIMASK调度器自检 | mask=1投递完成IRQ，再恢复0 | 屏蔽期间不回调、不改BSP状态；恢复后才完成 | 断言17项；最终IDLE；HAL同步/异步/Abort=0/1/0；IRQ交付/屏蔽挂起=1/1；PRIMASK=0 | PASS |
| E06 | TakeResult空指针 | 传输中及已完成时传NULL，再正常读取 | false且不消费结果，随后正常交付 | 断言17项；最终IDLE；HAL同步/异步/Abort=0/1/0；IRQ交付/屏蔽挂起=1/0；PRIMASK=0 | PASS |
| E07 | HAL_ERROR/TIMEOUT启动重试 | 完成前一事务，再模拟两种启动错误，最后成功启动 | 错误分别-9/-8并回IDLE，最后SYS_OK | 断言17项；最终TRANSFERRING；HAL同步/异步/Abort=0/4/0；IRQ交付/屏蔽挂起=1/0；PRIMASK=0 | PASS |
| E08 | Tick读取时完成竞态 | 100ms时在HAL_GetTick中投递完成IRQ | 不Abort、不覆盖完成状态，下次交付SYS_OK | 断言17项；最终IDLE；HAL同步/异步/Abort=0/1/0；IRQ交付/屏蔽挂起=1/0；PRIMASK=0 | PASS |
| E09 | CPSID前最后边界完成 | 100ms时在__disable_irq执行前投递完成IRQ | 不Abort，恢复PRIMASK，下次交付SYS_OK | 断言17项；最终IDLE；HAL同步/异步/Abort=0/1/0；IRQ交付/屏蔽挂起=1/0；PRIMASK=0 | PASS |
| E10 | 初始化拒绝错误时钟源 | RCC配置返回OK但模拟I2C2选择其他时钟源 | INVALID_STATE=-4；不执行HAL I2C初始化、不启用NVIC | 断言7项；最终IDLE；HAL同步/异步/Abort=0/0/0；IRQ交付/屏蔽挂起=0/0；PRIMASK=0 | PASS |
| E11 | 初始化拒绝错误频率 | D2PCLK1源正确，但PCLK1为60MHz | INVALID_STATE=-4；不执行HAL I2C初始化、不启用NVIC | 断言7项；最终IDLE；HAL同步/异步/Abort=0/0/0；IRQ交付/屏蔽挂起=0/0；PRIMASK=0 | PASS |
| E12 | 重复初始化时钟校验 | 120MHz重复Init，然后改变源、改变频率，再恢复 | 正常重复成功不重配硬件；两种时钟变更均拒绝；恢复后成功 | 断言8项；最终IDLE；HAL同步/异步/Abort=0/0/0；IRQ交付/屏蔽挂起=0/0；PRIMASK=0 | PASS |

总数 32，PASS 32，FAIL 0，未执行 0；断言 1069 项。

## 运行限制

Mock 不模拟 I2C 寄存器、总线电气行为或 HAL 内部中断分支。队列表示可交付的 HAL 回调事件，测试不等同于寄存器级外设仿真。
无 Abort 完成回调时保持 ABORTING 属于本次已接受设计；未增加恢复、二次超时或新状态机。
回调和 TakeResult 按当前主循环/中断使用场景测试，不支持多个前台调用者并发访问的保证。
真实 SCL、上拉、电源、SSD1306 应答和显示结果均未验证。

## 实际编译命令

```text
D:\32\ARM\ARMCC\Bin\armcc.exe --vsn
D:\32\ARM\ARMCC\Bin\armcc.exe --cpu Cortex-M4 --c99 -O0 -I D:\32\STM32H7\h7_mac\Tests\BspIicCommunication\stubs -I D:\32\STM32H7\h7_mac\CM4\BSP -I D:\32\STM32H7\h7_mac\CM4\Framework -I D:\32\STM32H7\h7_mac\Common\Inc -c D:\32\STM32H7\h7_mac\Tests\BspIicCommunication\communication_test.c -o D:\32\STM32H7\h7_mac\Tests\BspIicCommunication\communication_test.o
D:\32\ARM\ARMCC\Bin\armlink.exe D:\32\STM32H7\h7_mac\Tests\BspIicCommunication\communication_test.o --entry Communication_TestRun --scatter D:\32\STM32H7\h7_mac\Tests\BspIicCommunication\test.sct --keep=*(.framework_init) --no_scanlib -o D:\32\STM32H7\h7_mac\Tests\BspIicCommunication\communication_test.axf
```

Product: MDK Plus 5.32
Component: ARM Compiler 5.06 update 7 (build 960)
Tool: armcc [4d365d]
For support see http://www.arm.com/support
Software supplied by: ARM Limited

