# CM7移植FreeRTOS后USB枚举异常：从错误假设排查到WFI

记录日期：2026-11-03。开发基线：`codex/cm7-freertos-p0 @ 0dafe9b`。

**状态：根据后续实板对照反馈，已验证保留WFI、保持USB2控制器Sleep时钟开启并关闭ULPI Sleep时钟使能后，USB恢复正常枚举。内部硬件机制及长期稳定性仍未确认。**

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

## 后续验证：从WFI影响追到Sleep时钟配置

当时我先通过对照实验，发现执行WFI后USB枚举异常，取消WFI后恢复正常，因此确认问题与进入睡眠有关。

接着我怀疑是睡眠期间USB时钟被关闭，于是添加 `__HAL_RCC_USB2_OTG_FS_CLK_SLEEP_ENABLE()`，保持USB2控制器的Sleep时钟开启，但问题仍然存在。这说明仅保持控制器Sleep时钟开启不足以解决问题，并不能据此断言HSI48振荡器已经停止。

随后查阅资料，发现STM32H7使用内部FS PHY时，有关闭ULPI Sleep时钟使能的处理案例。我保留WFI和前面的配置，再添加 `__HAL_RCC_USB2_OTG_FS_ULPI_CLK_SLEEP_DISABLE()`，USB才恢复正常枚举。

```c
//Sleep期间保持USB2控制器时钟
__HAL_RCC_USB2_OTG_FS_CLK_SLEEP_ENABLE();

//内部FS PHY场景下关闭ULPI Sleep时钟使能
__HAL_RCC_USB2_OTG_FS_ULPI_CLK_SLEEP_DISABLE();
```

| 对照条件 | 用户反馈的实板结果 |
|---|---|
| 原Sleep时钟配置，执行WFI | USB枚举异常 |
| 取消WFI | USB恢复正常 |
| 保留WFI，仅添加USB2控制器Sleep时钟使能 | 仍然异常 |
| 保留WFI及控制器Sleep时钟使能，再关闭ULPI Sleep时钟使能 | USB恢复正常枚举 |

这次排查是逐步缩小范围的：**先发现WFI相关，再验证USB控制器的睡眠时钟，最后定位到ULPI Sleep时钟配置。**上述实板结果来自用户后续反馈，不是软件模拟结果。

### 对资料解释仍保留的疑问

查阅过程中也看到“USB核心与ULPI睡眠时钟同时开启会引发硬件冲突”的解释，但我对这个内部机制仍保持疑问，没有将它作为确定结论。我的对照实验确认的是：**当前硬件与配置下，关闭ULPI Sleep时钟使能解决了执行WFI后的USB枚举异常。**

“CPU执行WFI后永远无法唤醒”也没有得到本次证据支持；本次观察到的是USB枚举异常，不能扩大为CPU无法唤醒。

TinyUSB的STM32H7驱动在内部FS PHY初始化路径中也清除了ULPI Sleep时钟位，注释说明否则WFI/WFE会影响USB。这支持该配置处理，但没有解释芯片内部电路为何产生这种影响。

- [ST社区：H7 FS与WFI/SETUP接收案例](https://community.st.com/stm32cubemx-mcus-29/why-stm32h743-usb-fs-doesn-t-work-if-freertos-tickless-idle-enabled-70590)
- [TinyUSB：内部FS PHY的Sleep时钟处理](https://github.com/hathach/tinyusb/blob/master/src/portable/synopsys/dwc2/dwc2_stm32.h#L165-L176)

目前没有保存完整的RCC_AHB1LPENR/SCB_SCR现场快照，也没有直接测量睡眠期间USB内部时钟或完成长期稳定性验证。这里记录配置对照结果，不把“硬件冲突”或“时钟源被切断”写成已证实的内部机制。

## 调试中踩过的坑

- 外层断点暂停期间，主机仍可能继续发请求；解除暂停后读到的状态，不一定代表无断点运行时的到达时序。
- 调试器停住CPU会造成主机控制传输超时。不能在长期单步的同一次插入中判断真实枚举稳定性。
- GRXSTSP读取会弹出FIFO状态。不要放在Watch或打开会自动读取它的外设窗口；看HAL读入的RegVal。
- 局部变量cannot evaluate可能是优化和作用域问题，不是硬件报错；改看模块持久状态及实际分支。
- 同名no.pcapng在过程中被多次覆盖。应按采集时间和目标设备区分现场，不可混用不同版本的帧号或地址。
- 原软件验收482项与固件审查44项没有真实模拟WFI导致的芯片睡眠时钟行为。软件PASS没有覆盖这个硬件闸门。

## 面试60～90秒复述

> 我做CM7 FreeRTOS最小移植时，保留了原业务顺序，但实板出现相机采集正常、USB描述符请求失败。起初我怀疑调度器启动和USB连接时序，做了调度器稳定后再插USB的对照，问题仍在。我随后沿USB IRQ、HAL Reset、EP0、SETUP逐层查，确认Reset和EP0重建正常。中间有一次因为回调断点没命中，我以为没收到SETUP，但解码FIFO状态0x008C0080后发现它就是EP0的8字节SETUP，推翻了这个判断。后来观察到外层断点暂停会改变接收行为，我才回看移植差异，发现Idle Hook额外加入了WFI。通过开关WFI的对照，我确认问题与进入睡眠有关。接着怀疑USB睡眠时钟被关闭，添加控制器Sleep时钟使能，但仍然失败。随后查资料发现内部FS PHY场景下的ULPI Sleep时钟注意事项，于是保留WFI，再关闭ULPI Sleep时钟使能，USB才恢复正常枚举。实验定位到了该配置条件，但对于资料中“两个时钟产生硬件冲突”的解释，我没有足够证据，因此保留疑问。这次让我认识到，要用实验不断修正假设，也要区分RTOS调度与额外引入的低功耗行为。

## 面试追问准备

| 追问 | 回答重点 |
|---|---|
| 为什么不一开始改描述符？ | 当时原因未知；先确认请求接收和失败阶段，避免在无证据时改协议 |
| IRQ进了为何还要查？ | 可能是SOF；入口条件、屏蔽位、事件类型和回调交付各自都需验证 |
| WFI为何是后面才想到？ | 前面的时序/EP0假设没有解释现象，断点敏感才促使回看“裸机与RTOS的额外差异” |
| 最后验证到哪一层？ | 实板对照显示：保留WFI，仅保持控制器Sleep时钟仍失败，再关闭ULPI Sleep时钟使能后恢复；内部电路机制及长期稳定性未确认 |
