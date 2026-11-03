# CM7移植FreeRTOS后USB枚举异常：从错误假设排查到WFI

记录日期：2026-11-03。开发基线：`codex/cm7-freertos-p0 @ 0dafe9b`。

**状态：已发现WFI相关的重点线索，禁用WFI后的阶段已看到EP0协商恢复；尚未完成严格的恢复WFI再次复现及睡眠时钟机制验证。**

## 先回忆当时的困惑

移植前，裸机能进行相机采集和UVC输出。移植后，采集仍能运行，Windows却提示设备描述符请求失败。我起初并不知道是WFI，也没有直接得到一个正确假设，而是沿着初始化、启动时序、USB IRQ、Reset、EP0和SETUP逐层排查。

本次相机配置为160×120 YUY2，38400B/帧，**目标输出25 FPS**。25是配置及协商值，不是测出来的成功输出帧率。

## 真实排查顺序：每一步为什么改变方向

| 顺序 | 当时的判断或疑问 | 为什么这样怀疑 | 采取的动作和证据 | 当时能得出的结论 |
|---|---|---|---|---|
| 1 | 是否调度器没启动、任务卡死或断言关了中断？ | 新增了RTOS，且USB无法识别 | 提出检查init_error、g_rtos_debug和HAL Tick；现场反馈图像采集正常 | 采集正常不足以证明USB正常；没有完整保留所有计数快照，不能写成逐项实测排除 |
| 2 | USB会不会在调度器启动前连接，造成启动竞态？ | 源码显示Framework APPLICATION阶段先Start USB，之后才启动RTOS | 保持供电，等调度器运行后再插USB；反馈仍失败 | “仅因为连接过早”不足以解释故障，没有因此直接重排初始化 |
| 3 | 是否描述符内容或响应路径没有就绪？ | Windows提示设备描述符请求失败 | 先核对初始化及控制请求接收路径，没有直接修改HAL或协议 | 错误提示不能直接定位原因，继续确认请求是否真的到达MCU |
| 4 | 主机请求是否根本没到板卡？ | Windows说请求失败，但SETUP回调没有命中 | 第一次抓包没有目标1209:0001，只有其他设备；重新确认抓取对象和插入过程 | 不能把其他设备的GET_DESCRIPTOR当作本板请求；主机URB不等于STM32收到SETUP |
| 5 | USB IRQ是否未启用，或HAL入口被挡住？ | 回调不进，仍可能停在硬件/中断入口 | 现场确认USB IRQ进入；s_ready=1、s_started=1 | BSP入口条件成立，继续向HAL内部查；IRQ进入也可能只是SOF |
| 6 | 是否没有总线Reset或速度枚举完成？ | HAL_PCD_ResetCallback最初没命中 | IRQ入口读取GINTSTS=0x4400B038、GINTMSK=0x803C3818，交集0x3018 | USBRST、ENUMDNE、RXFLVL、SOF均有有效事件，不能继续说“硬件没有枚举事件” |
| 7 | HAL是否卡在FIFO读取或Reset处理？ | 有事件却没观察到回调 | 单步确认可进入ENUMDNE，随后跟到Usb_ReleaseEndpoints | Reset回调实际已经进入Platform；先前“回调没进入”只是当时的断点观察，不是最终事实 |
| 8 | Reset后EP0没打开，或故障状态锁存？ | 后续控制发送会受端点状态和stop_required拦截 | EP0 OUT/IN opened均为1；局部reset_result/release_result不可见，改看s_stop_required、s_last_result、s_operation_result，反馈正常 | 当次Reset后的资源状态正常，转查SETUP接收与交付，而非继续猜端点重建 |
| 9 | 是否只收到了普通OUT数据，没有收到SETUP？ | USB_ReadPacket的dest/len正常，但SETUP读取行和回调未命中 | 读取HAL已弹出的局部RegVal=0x008C0080，解码为EP0、8B、状态6 | 状态6就是STS_SETUP_UPDT，证明至少这次HAL观察到了SETUP数据更新；推翻“完全没收到SETUP” |
| 10 | 为什么外层断点能进，分支内断点却不命中？ | RegVal应满足条件，源码单步表现却矛盾 | 对比外层RXFLVL断点与分支内断点，观察到暂停会改变行为 | 调试器可能改变到达时序，也需留意优化/符号映射；不能由源码行号跳过直接认定条件计算错误 |
| 11 | RTOS新增的CPU睡眠是否影响USB？ | 前面排查没有定位到描述符或EP0内容错误；断点敏感促使回看移植差异 | 发现Idle Hook增加了__WFI，而裸机不主动睡眠；找到ST社区相似H7 FS案例；本地随后仅注释WFI，保留vTaskDelay(1) | 形成有依据的单因素对照，WFI是新增兼容性变量，不是最初就知道的答案 |

## 寄存器证据怎样解读

```text
GINTSTS & GINTMSK = 0x00003018
    0x00000008：SOF
    0x00000010：RX FIFO非空
    0x00001000：USBRST
    0x00002000：ENUMDNE
```

GINTSTS最低位为0，符合Device模式。这里的ENUMDNE是速度枚举完成事件，**不是Windows已经完成整个设备枚举**。

```c
RegVal = 0x008C0080;
endpoint    = RegVal & 0x0F;          //0
byte_count  = (RegVal >> 4) & 0x7FF;  //8
packet_type = (RegVal >> 17) & 0x0F;  //6，STS_SETUP_UPDT
```

“FIFO有SETUP数据”“STUP完成中断到达”“HAL交付Setup回调”“Core正确响应”是不同阶段。状态记录可以推翻没收到任何SETUP的判断，但不能独自证明后面所有阶段成功。

## 对照变化：只关闭额外睡眠，不关闭调度器

```c
void vApplicationIdleHook(void)
{
    g_rtos_debug.idle_iterations++;

    //本轮对照只注释这一行
    //__WFI();
}
```

保留Legacy任务的vTaskDelay(1)，Framework → USB Mode → UVC Process → App_Run顺序和帧所有权协议不变。没有为了让枚举成功而修改HAL、放宽端点校验或在USB ISR中添加延时。

FreeRTOS并不要求应用必须在Idle Hook执行WFI。configUSE_TICKLESS_IDLE=0也不等于CPU绝不会睡眠：自定义Idle Hook仍可主动执行WFI。

**本次证据边界：**本地未提交差异能够确认WFI被注释；后续抓包确认EP0 Probe/Commit控制传输成功。但没有完整记录同一供电/USB拓扑下“开WFI失败—关WFI成功—恢复WFI再失败”的三组无断点实验，因此应说“收敛到WFI相关睡眠兼容性”，不能说已经证明唯一根因或所有硬件机制。

## 机制线索与没有做过的验证

ST社区的H743 FS案例报告：执行WFI后无法完成枚举，关闭USB2 ULPI sleep clock位后恢复。它为本次排查提供方向，不是本板H747的直接测量结果。

本次没有读取并保存RCC_AHB1LPENR/SCB_SCR现场，没有实测USB睡眠时钟，没有实施并验证ULPI睡眠时钟位修复，也没有完成长期USB睡眠兼容性验证。面试时不要把这些讲成已完成动作。

[ST社区：H7 FS与WFI/SETUP接收案例](https://community.st.com/stm32cubemx-mcus-29/why-stm32h743-usb-fs-doesn-t-work-if-freertos-tickless-idle-enabled-70590)

## 调试中踩过的坑

- 外层断点暂停期间，主机仍可能继续发请求；解除暂停后读到的状态，不一定代表无断点运行时的到达时序。
- 调试器停住CPU会造成主机控制传输超时。不能在长期单步的同一次插入中判断真实枚举稳定性。
- GRXSTSP读取会弹出FIFO状态。不要放在Watch或打开会自动读取它的外设窗口；看HAL读入的RegVal。
- 局部变量cannot evaluate可能是优化和作用域问题，不是硬件报错；改看模块持久状态及实际分支。
- 同名no.pcapng在过程中被多次覆盖。应按采集时间和目标设备区分现场，不可混用不同版本的帧号或地址。
- 原软件验收482项与固件审查44项没有真实模拟WFI导致的芯片睡眠时钟行为。软件PASS没有覆盖这个硬件闸门。

## 面试60～90秒复述

> 我做CM7 FreeRTOS最小移植时，保留了原业务顺序，但实板出现相机采集正常、USB描述符请求失败。起初我怀疑调度器启动和USB连接时序，做了调度器稳定后再插USB的对照，问题仍在。我随后沿USB IRQ、HAL Reset、EP0、SETUP逐层查，确认Reset和EP0重建正常。中间有一次因为回调断点没命中，我以为没收到SETUP，但解码FIFO状态0x008C0080后发现它就是EP0的8字节SETUP，推翻了这个判断。后来观察到外层断点暂停会改变接收行为，我才回看移植差异，发现Idle Hook额外加入了WFI。关闭这一睡眠变量后的阶段，后续抓包已经能看到正常Probe/Commit，所以排查方向收敛到了WFI相关的睡眠兼容性。不过恢复WFI再次复现及睡眠时钟机制还需要继续验证。这次让我认识到，要用实验不断修正假设，也要区分RTOS调度与额外引入的低功耗行为。

## 面试追问准备

| 追问 | 回答重点 |
|---|---|
| 为什么不一开始改描述符？ | 当时原因未知；先确认请求接收和失败阶段，避免在无证据时改协议 |
| IRQ进了为何还要查？ | 可能是SOF；入口条件、屏蔽位、事件类型和回调交付各自都需验证 |
| WFI为何是后面才想到？ | 前面的时序/EP0假设没有解释现象，断点敏感才促使回看“裸机与RTOS的额外差异” |
| 是否已经证明睡眠时钟位就是根因？ | 没有；这是同类案例给出的机制线索，本板仍需恢复对照和寄存器/硬件验证 |
