# TinyUSB replacement：最小修复边界与剩余门禁

## 不采用的修复

VERIFIED：真实edpt_disable和FIFO Flush是void，既有USBD会在调用后继续调整BUSY/CLAIMED或类状态；仅加入超时return会失去失败事实。仅使用watchdog重启、预先清EPENA、跳过disable、强行pop GRXSTSP、清Frame引用或恢复旧UDLM均不满足本任务。

## 候选最小上游补丁（仅提案，未实现）

INFERRED：优先向TinyUSB现有DCD契约增加**同步、有界且能传播失败**的结果，而不是建立另一个控制器框架。需要可核对的改动包括：

1. DWC2内部disable/flush/reset返回真实成功或超时/硬件失败；保留调用前后的寄存器事实，不以返回值掩盖仍在途Buffer。Task和ISR均不能无限轮询；ISR优先拒绝不能安全完成的操作，向现有事件队列报告失败。
2. 当前公开abort bool的false意为“不支持并回退STALL”，不能拿它表示“已部分修改硬件但失败”。至少需要区分未支持、完成、真实失败；Core在真实失败时不能再自动STALL/clear并清claim。该接口扩展可能涉及TinyUSB device/dcd.h与device/usbd.c；必须列出最小Diff和所有类调用者，不能只在DCD一处假成功。
3. 同理现有close_all/iso_activate、deinit应在没有停止证明时拒绝上层的释放/新配置。沿用TinyUSB的队列和Task，不新增StopAttempt/ParticipantRegistry。共享RX消费只由DCD真实RXFLVL路径完成，保留EP0 SETUP，不用单端点RX flush规避。
4. H747 PIO ZLP在DWC2的实际所有零长度IN启动路径落实ES0445 Rev7 §2.24.5；在合格时基下证明AHB间隔，不能只放一段CPU延时。原子命令发布、短临界区和中断嵌套也需验证，不能移植旧Lifecycle框架替代勘误。
5. 最后以本轮8项反例作负向起点，增加“超时返回但Core不清旧引用/不启动下一事务”的集成测试；没有这一层，就不批准硬件运行。

UNRESOLVED：这些是具体后续边界，并非已有可提交DCD修复。本轮DCD Diff为0。仅修改DWC2内部循环不足以独立解决失败传播，故按任务安全门禁停止受阻路径；未擅自重写TinyUSB Core或换另一栈。

## FreeRTOS与ARMCC5

VERIFIED：当前FreeRTOSConfig.h:29的configUSE_MUTEXES=0，而TinyUSB原生FreeRTOS OSAL需要静态Mutex。不能把缺失API声明为extern或用永远成功的空锁消除告警。可研究独立新构建配置启用真实静态Mutex并重新编译同一内核，需评估现任务及优先级继承影响；另一选择是符合单Task实际访问约束的上游OSAL适配，不能把OS_NONE当FreeRTOS替代方案。保持原工程配置，不用全局宏重定义伪造kernel已具备Mutex。

INFERRED：pdTICKS_TO_MS可用有界64位乘除兼容宏补齐，需覆盖整数溢出/不同tickrate；它不能解决Mutex或DCD门禁。本轮保留实际告警作为证据，不无谓改变运行时配置。

VERIFIED：ARMCC5 bmHint ABI补丁只有一行，uint8变uint16，符合UVC两字节字段和上游原有48字节断言；真实ARM检查sizeof=48、bFormatIndex偏移2、bmLayoutPerStream偏移40。无全局pack/no-warn/删除assert。早期47字节失败保留于compile-history。

## 未来生产构建接口清单（尚未接通）

| 工程边界 | 必要适配 | 不允许共存的旧路径 |
| --- | --- | --- |
| BoardInit/BoardDeInit | 从bsp_usb.c:1595/:1880附近提取GPIOA、AF10、HSI48/CRS、USB电源及RCC；不触发HAL_PCD_Init | HAL PCD控制器初始化及FIFO写入 |
| OTG_FS vector | 唯一入口tusb_int_handler(0,true)，优先级6，使用TinyUSB队列 | BSP旧IRQ→HAL_PCD_IRQHandler |
| UsbTask | 原任务非阻塞tud_task_ext(0,false)，既有事件唤醒；停止也由该Task走TinyUSB契约 | 并行旧USB_Device_Stop/Mode协议执行器 |
| Mode | RAM/SD/UVC互斥启动，先排空业务消费者，再按真实TinyUSB关闭能力切换 | 旧ST Core RegisterClass及双栈操作 |
| MSC | 原RAM Backend或SD Worker的薄回调，旧pending_io完成只通知一次 | usb_msc_bot.c / usb_msc_scsi.c实现 |
| UVC | 保留Frame Bridge，单Task源Frame归还/USB payload分开；线性或明确拷贝边界 | 旧Class/Payload状态机与新Video Class并行 |

INFERRED（预算示例）：按H7 9EP、PIO、64B EP0、64B Bulk且不双缓冲，源码公式得RX约66 words；EP0和MSC IN最少各16 words；ISO 512B单IN时RX约49 words、EP0 TX16、ISO TX128。实际默认分配、FIFO数量上限/洞、GHWCFG和版本参数仍须用真实初始化及寄存器核对，这些不是已运行的硬件FIFO结果。Video 38400B单帧拷贝缓存若采用需另外做RAM预算，不更改Linker来掩盖溢出。

## 阻塞解除顺序

INFERRED：先证明有界DCD失败传播及ZLP时序，再解决真实FreeRTOS配置/唯一IRQ与Task，做完整RAM构建及协议回归；RAM停止/重启合格后接SD只读Worker；ISO/Frame边界闭合后接Camera。当前没有满足完整RAM前置条件，后续两条硬件路径不执行。NOT_TESTED：上述实板时序、挂起/恢复、设备重连与视频吞吐。
