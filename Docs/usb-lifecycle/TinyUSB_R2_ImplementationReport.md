# TinyUSB R2 实施报告

日期：2026-11-21。基线 `afbcf60e98bc178992b7e5adb714449608e3145a`；研究分支 `codex/usb-tinyusb-p1-replacement`。

**总体：BLOCKED，R2尚未完成。VERIFIED：完整RAM MSC固件已构建链接、FreeRTOS/单IRQ隔离已接通。活动Bus Reset、活动Disconnect、直接活动组关闭和DeInit失败时IRQ顺序仍未闭环。不得将模型中的安全拒绝当成正常恢复成功。**

## 完整构建与交付

VERIFIED：独立配置 `MDK-ARM/h7_mac_TinyUSB_RAM.uvprojx`，target `TinyUSB_RAM_CM7`；ARM Compiler5.06 update7 build960；STM32H747XIHx/CM7，原工程同一芯片族，FS PHY / USB2_OTG_FS / Slave PIO / FreeRTOS / TinyUSB MSC / 原16扇区RAM Backend。不是18对象组件编译。

VERIFIED：`python Tests/TinyUSBReplacement/build_r2.py` 从原Keil CM7配置派生独立工程，完整重编Startup、SystemClock、HAL、RTOS、Port、UsbTask、TinyUSB、RAM Backend并链接。结果 **0 Error(s), 0 Warning(s)**。Code45658、RO1086、RW284、ZI25788字节；链接无重复/未解析符号。独立scatter使用原CM7的0x08000000 Flash、0x20000000 DTCM范围，未修改原scatter或MPU配置。没有SD/共享IPC数据段用于实验LUN。

VERIFIED：固件交付目录：`D:\32\STM32H7\h7_mac_lifecycle\output\TinyUSBReplacement\R2-review-3c70c67e66b3`，包含 `h7_mac_CM7_TinyUSB_RAM.axf/.map/.hex` 和build.log。
AXF SHA256：`3c70c67e66b3457130a8885b83baab66eb27fce12bdcdb56073aaac805accb29`。
构建原始目录：`D:\32\STM32H7\h7_mac_lifecycle\output\TinyUSBReplacement\r2-firmware-1795250620068566200`。产物留在ignored output，不加入Git。完整构建参数、源文件哈希、原项目/RTE/options保留哈希见[r2-build.json](../../Tests/TinyUSBReplacement/reports/r2-build.json)；[链接和门禁](../../Tests/TinyUSBReplacement/reports/R2_link_and_gates.json)。

VERIFIED：没有改原 `MDK-ARM/h7_mac.uvprojx`、uvoptx、原RTE头或CM4。新配置禁用自动下载/外部program_cm4脚本；未调用烧录命令。uVision产生的新实验RTE/debug配置留本地，不提交本机调试配置。

## FreeRTOS和IRQ隔离

VERIFIED：`CM7/Platform/FreeRTOSConfig.h:29` 仅在实验宏 `TINYUSB_RAM_EXPERIMENT=1` 下开启Mutex；默认仍为0。静态分配保持1、动态分配0。实验OSAL确实为 `OPT_OS_FREERTOS`，没有使用OPT_OS_NONE伪装集成。OSAL没有pdTICKS_TO_MS时使用uint64比例换算；项目关闭Mutex的未使用API返回NULL，实验实际使用真实静态优先级继承Mutex和静态队列。

VERIFIED：原57警告的FreeRTOS接口缺失已逐项消除；DWC2 enum超int警告改为uint32常量，没有屏蔽警告或更改警告级别。组件复编也为0错误0警告。并非把历史FAIL删掉。

VERIFIED：唯一控制器入口在 `CM7/TinyUSB/tinyusb_experiment.c:60`，向量117为 `0x8002d69`，与强符号完全相同，只转发TinyUSB DCD。最终ELF无HAL_PCD_Init/IRQ、BSP_USB_Start、USB_Mode_Start、USBD_Init、CameraTask_Create、StorageTask_Create或pvPortMalloc。工程仅排除PCD/USB旧owner相关源；其他HAL依赖保留编译，未修改HAL/LL/ST Core。

VERIFIED：`usb_task.c`复用原静态TCB、2048 words栈、优先级4、原UsbTask_Create入口；没有第二个USB Task。实验宏下仅其Run分支进入TinyUSB循环，不进入旧App_USB/USB_Mode/Camera/Storage业务。原main的实验分支在SystemClock完成后准备端口、创建该任务并启动原FreeRTOS；CM4保持原启动握手的休眠阶段，未改CM4程序。默认分支字节变更仅增加编译门控，旧流程保留。没有重构原业务启动架构。

VERIFIED（源码/完整AXF模型）：OTG_FS逻辑优先级5，对应0x50，等于configMAX_SYSCALL_INTERRUPT_PRIORITY；优先级组3、4个优先级位。实际FreeRTOS Port的vPortEnterCritical/ExitCritical使嵌套Task spin的BASEPRI保持0x50，最后退出恢复0。USB IRQ在该阈值下不能抢占；单CM7 ISR分支不阻塞。CM4不运行此USB路径。NOT_TESTED：实际抢占、RTOS调度负载与硅片中断行为。

## DWT、Tick与RAM链

VERIFIED：端口 `TinyUSB_ExperimentPrepare()` 调用SystemCoreClockUpdate，要求480MHz，与现有HSI64/M4/N60/P2、AHB/2配置一致；不改PLL/HCLK。启用TRCENA/CYCCNTENA后检查计数递增。USB沿用原HSI48+USB2 SOF CRS、PA11/12和供电配置。NOT_TESTED：实板电源/时钟正确性和DWT持续运行。

VERIFIED：原FreeRTOS runtime stats关闭，本轮不伪称可用。DWT会受CPU睡眠影响，因此异步取消新增原TinyUSB时间API的50ms Tick期限，与原DWT累计期限并列；FreeRTOS tick提供跨Idle WFI进度，原时基失效路径仍FAILED。Task循环处理tud_task_ext、RAM完成并vTaskDelay(1)，没有新Task。时基稳定性、回绕、Tick前进而CPU计数较少的模型均覆盖。NOT_TESTED：实板50ms精度、调度延迟和饥饿。

VERIFIED：RAM后端仍是原 `usb_msc_ram.c`，16x512字节，可写仅RAM。真实完整AXF已通过描述符、SET_ADDRESS/CONFIGURATION、GET_MAX_LUN、INQUIRY、TUR、容量、REQUEST SENSE、READ/WRITE回读、BOT Reset和Clear HALT的MMIO模型验证。

VERIFIED（官方来源）：[pid.codes 1209:0001](https://pid.codes/1209/0001/)为非唯一的内部测试PID，不能用于分发、销售或制造设备；已在描述符源码增加该警示。本项目未取得正式独占产品PID，本轮不宣称正式产品USB身份。

## 生命周期的实际完成边界

| 路径 | 结果 |
| --- | --- |
| 空闲、无RX残留的Bus Reset → EP0 → SET_CONFIGURATION → CBW | VERIFIED：完整AXF模型成功；不代表活动端点Reset |
| Bulk已由R1B取消SUCCESS → Controller DeInit/Init | VERIFIED：完整AXF模型成功；33项中原10轮测试继续通过 |
| 所有非零端点已退休 → close_all | VERIFIED：新两遍预检查后关闭；无共享RX Flush、不改变EP0、不丢FIFO reservation，同RAM端点可重开 |
| 直接关闭活动组 | BLOCKED：legacy void接口不能报告PENDING，保留source/busy并fault |
| 活动Bulk Bus Reset | BLOCKED：RM未说明USBRST自动清EPENA；现实现检测未停硬件即锁定，不能直接移除检查 |
| 活动Disconnect | BLOCKED：新增守卫阻止Core在硬件仍使能时清Class状态；保留锁定，未实现重新枚举 |
| DeInit失败 | BLOCKED：旧路径可能已先关IRQ/断开；未证明可继续推进，不增加假恢复 |
| EP0 active IN抢占/PIO ZLP | BLOCKED：未核验正常退休及勘误，本轮不猜测时序 |

UNRESOLVED：要关闭这些门禁，需在TinyUSB原DCD/Core事件机制中建立Reset/Close的异步完成资格及EP0/共享RX停止证据；不能套用普通非零Bulk取消，也不能引入旧UDLM或强制Reset作为万能恢复。本轮暂停受阻的正常恢复实施，保留已完成的隔离完整固件与全部反例，等待集中人工审核。

实际生产Diff见[R2_production.patch](../../Tests/TinyUSBReplacement/reports/R2_production.patch)，固定上游差异见[R2_upstream.patch](../../Tests/TinyUSBReplacement/reports/R2_upstream.patch)。没有烧录，没有接入SD/UVC完整移植，没有合并main或修改旧P1分支。
