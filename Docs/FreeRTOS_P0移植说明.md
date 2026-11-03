# CM7 FreeRTOS P0 最小移植

## 基线及边界

开发分支：codex/cm7-freertos-p0；主线基线 bffacd28897b47c7e54b90562960ff365f6b7695。开始时用户已将usb_uvc_desc.h中的长度改为24，本轮保留并完成一致性检查。uvoptx、output/、tmp/不提交，不合并main。

裸机基线为160×120 YUY2、38400B/帧、USB FS ISO IN 1023B、**目标输出25 FPS**。这只是配置值；没有当前真实FPS、PCB版本确认、30分钟预览、丢帧及复位原因的测量记录。不能把软件PASS或25配置值当成实测性能。baremetal_baseline.json保留缺失项。

先修复字符串：Roxy Camera为11个UTF-16LE字符，长度2+11×2=24B。数组容量由实际初始化数据推导，bLength和返回长度使用USB_UVC_FUNCTION_STR_DESC_SIZE，并做编译期一致性检查。静态地址4字节对齐，最后PIO读取为偏移20..23，没有22B旧值或越界padding。无改动VID/PID、配置长度180B或视频参数。

## 官方内核与编译器

固定FreeRTOS-Kernel V10.6.2，使用portable/RVDS/ARM_CM7/r0p1，适配Keil ARMCC5与M7 FPU上下文。使用官方tasks/list/queue及Port；不使用GCC Port，不复制CM4F Port，不修改官方内核。upstream.json保存来源、版本、归档和逐文件SHA256、MIT许可。

选择此版本是为了建立可复现的ARMCC5基线，不代表已经完成未来内核升级/长期维护审查。若更换芯片或Port，必须重新核对内核修订、FPU和异常上下文。

首编译遇到configMAX_SYSCALL_INTERRUPT_PRIORITY展开为含U后缀的表达式，ARMCC5内联汇编mov操作数不接受；改为整数常量移位(5 << 4)，保留数值0x50。未修改port.c规避错误。

## 启动和调用关系

```text
Reset / 原双核启动 / HAL_Init(TIM6 Tick) / SystemClock_Config
    → 原HSEM唤醒、MPU、Framework_InitRuntime
    → Platform_RtosStart(Main_Run)
        → xTaskCreateStatic(LegacyVideo)
        → vTaskStartScheduler
            → 官方SVC首次恢复任务
            → LegacyVideo：Main_Run → vTaskDelay(1)
            → IdleTask：空闲Hook → WFI
```

Main_Run内部顺序保持Framework_Run → USB_Mode_Process → USB_UVC_Process → App_Run。任务每轮阻塞一个1ms Tick；执行耗时另算，不能保证精确1ms处理周期。暂不拆USB/Camera任务，不新增相机桥接逻辑，不修改Media/UVC帧所有权协议。

Framework初始化仍在调度器之前，原有HAL阻塞初始化由TIM6提供时间。初始化阶段不使用RTOS等待或队列。本轮不声称已完成P0.3多任务隔离。

单个Legacy任务是USB、桥接及业务状态机的唯一线程调用者。USB的IRQ屏蔽锁仍只负责USB ISR隔离，不是任务互斥锁；未来多任务访问前必须另做所有权及锁设计。DCMI/USB ISR继续使用原标志和回调，不新增任何FromISR调用。

## 异常与时基所有权

| 入口 | 唯一执行者 | 作用 |
| --- | --- | --- |
| SVC_Handler | 官方RVDS Port，宏映射 | 首次恢复PSP任务上下文 |
| PendSV_Handler | 官方RVDS Port，宏映射 | 保存/恢复任务上下文及FPU扩展上下文 |
| SysTick_Handler | CM7 it.c调用xPortSysTickHandler | 1kHz内核Tick；不再调用HAL_IncTick |
| TIM6_DAC_IRQHandler | platform_hal_timebase.c | 独立HAL_IncTick，调度器前后均运行 |

HAL_InitTick强实现会覆盖官方weak版本；HAL_Init和HAL_RCC_ClockConfig均可重新配置TIM6。按HCLK、PCLK1和TIMPRE推导时钟，先分到1MHz再ARR=999形成1ms。当前HCLK240MHz/PCLK1120MHz、TIMPRE=0时TIM6为240MHz，PSC=239；启动HSI64MHz时PSC=63。非整MHz或超出分频范围返回HAL_ERROR。TIM6未被CM4或其他已配置外设占用，不启用HAL TIM模块、不改变PLL。

HAL_SuspendTick/ResumeTick只控制TIM6 UIE，不停止RTOS Tick；FreeRTOS调度暂停也不停止HAL Tick。当前未启用tickless和低功耗迁移。

## IRQ规则

H747有4个优先级位，NVIC_PRIORITYGROUP_4为AIRCR PRIGROUP=3。启动时断言分组和位数。逻辑优先级如下，数值越小优先级越高：

| IRQ | 优先级 | 约束 |
| --- | ---: | --- |
| TIM6 HAL Tick | 4 | 高于BASEPRI阈值5；严禁RTOS API，仅计数 |
| DCMI / DMA1 Stream0 | 5 | 保留原配置；本轮不调用RTOS API |
| USB OTG_FS | 6 | 保留原配置；本轮不调用RTOS API |
| PendSV / SysTick | 15 | 由官方Port设为最低 |
| SVC | 复位默认0 | 官方Port首次启动使用，不作为业务ISR |

可调用FromISR的中断必须满足逻辑优先级>=5，仍要逐个API确认适用上下文。既有PRIMASK全屏蔽临界区保留；BASEPRI不能代替全局资源所有权，长时间PRIMASK仍会阻塞Tick和USB。

## 内存和观察变量

- LegacyVideo：静态2048 Word，8KiB，任务优先级3。
- Idle：静态256 Word，1KiB，任务优先级0。
- 内核动态分配关闭；不链接heap_1/4，不使用标准malloc，也不引入内核堆。
- 原USB固定Class池不变，Media帧仍位于原SDRAM；RTOS TCB/栈使用CM7原DTCM RW/ZI区，不使用IPC共享区。
- CM7启动MSP/ISR栈由1KiB增为4KiB，用于既有初始化和异常嵌套；CM4启动栈不变。
- 8KiB/4KiB是初始预算，不是实际最大用量证明。需在实板压力场景看水位。

Keil观察g_rtos_debug：started、tick_count、legacy_iterations、idle_iterations、legacy_stack_free_words、idle_stack_free_words。水位单位是Word，每1000轮采样，0也可能表示尚未采样。fault_file/fault_line记录内核断言或栈溢出；cfsr/hfsr保存HardFault/断言时SCB状态。异常停机不自动恢复，不释放仍借用的帧。

## CubeMX再生成

新增Platform文件独立于生成目录，main调用、SysTick转交和HardFault记录位于USER CODE区。为让Port唯一拥有SVC/PendSV，移除了生成it.c的两个空函数；MSP大小在CM7启动汇编中修改。这两处不是USER CODE可保护区域。CubeMX若重建it.c或启动文件，必须复查并移除重复SVC/PendSV定义、恢复4KiB MSP；固件向量测试会检查最终链接归属。不能声称再生成完全无需审查。

## 软件与实板验收边界

测试命令、逐例结果和编译日志见Tests/FreeRtosP0/README.md。Unicorn执行真实ARMCC5内核及Port代码，但调度器首次SVC入口由测试钩子替代；阻塞/唤醒通过显式vTaskSwitchContext验证，真实SysTick处理函数及TIM6逻辑执行。不能把它当作芯片NVIC异常进入/退出、FPU抢占、实板调度及USB枚举成功证明。

正式固件另检查向量中的SVC/PendSV/SysTick/TIM6地址、RW/ZI和栈布局；官方文件SHA一致。

**待实板闸门**：先获取裸机25 FPS配置下的真实成功FPS/丢帧/错误基线，再运行RTOS；观察1ms双Tick、任务与Idle水位、断言/HardFault、USB Reset/Alt0/Disconnect/Abort、CM4 OLED。Windows连续预览至少30分钟，真实FPS、DMA错误、帧所有权恢复应不劣于基线。未完成前P0硬件验收不通过，不推进拆任务或JPEG。

共享Keil工程中为CM4添加同名但IncludeInBuild=0的RTOS Group，避免uVision同步Group时误编译CM7内核。CM4源文件、Include Path、宏、启动向量和Scatter均不变。初次CM4构建曾暴露该Group同步问题，已修复排除配置后重新完整构建。
