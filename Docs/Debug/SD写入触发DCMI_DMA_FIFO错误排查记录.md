# SD写入触发DCMI DMA FIFO错误：从首次错误追到FatFS隐式复制

记录日期：2026-11-10。回顾Stage08D；地址和寄存器属于对应历史镜像。

**状态：定位了Storage路径中遗漏的SDRAM复制，并增加FatFS输入中转；没有证明FMC竞争是唯一硬件根因，也没有完成严格的长期稳定性闭环。**

本篇只记录“写SD时采集报错”，不记录录像帧率不足、早期UVC黑屏或IPC界面问题。

## 先回忆当时的困惑

我原本已经能运行相机和USB，加入SD保存后，却出现Camera停止、USB随之停止。起初不知道是文件写入失败、帧归还错误，还是采集硬件先报错。

我没有把Storage最终result直接当成原因，而是检查Control收到的FAULT来自谁，再捕获首次DCMI/DMA错误。后续逐步缩小场景，不是一开始就认定SDRAM总线竞争。

## 真实排查顺序

| 顺序 | 当时的疑问 | 动作和证据 | 判断怎样改变 |
| --- | --- | --- | --- |
| 1 | 是Storage自己失败，还是Camera先故障？ | 核对Storage、Camera、USB、Control状态，确认FAULT来源Camera，result=0xFFFFFFF7 | 最终停止状态不等于首次原因，转查采集错误 |
| 2 | 是DCMI溢出还是DMA故障？ | 首次Dcmi_RecordError捕获s_event_dma_error=2，调用栈为DMA IRQ→Dcmi_CheckErrors→Dcmi_RecordError | 按当前HAL定义是DMA FE，不能直接称为DCMI FIFO溢出 |
| 3 | 是否SDMMC IDMA正与采集DMA竞争？ | 用户确认尚未开始本次写数据；现场SDMMC1->IDMACTRL=0 | 至少这次错误不要求SDMMC IDMA正在搬运，原先解释不够 |
| 4 | 哪个任务在执行什么？ | pxCurrentTCB显示Storage，IRQ下方出现_rt_memcpy_w | 范围缩小到Storage复制附近，但被打断不等于因果证明 |
| 5 | SDRAM整扇区直达IDMA能否解决？ | 试过满足条件的SDRAM直达，其余AXI中转；仍命中错误，之后按要求撤回 | 不能把这次尝试当成最终方案或成功证据 |
| 6 | 是不是复制时出现FE？ | 增加首次快照：复制阶段、源/目的、长度、复制前和错误时DMA标志 | 不再依赖Stop清理后的寄存器值 |
| 7 | 1KiB分块加1ms间隔是否足够？ | 一次快照显示1024B SDRAM→AXI复制中，LISR从0x10变0x11，IDMA未开启 | 小块和间隔没有自动消除问题，需继续看复制方式和覆盖范围 |
| 8 | 库memcpy访问方式是否有关？ | 仅把受控复制改成volatile逐字节访问，保留当轮长度、间隔；仍报错，另一快照active=0 | 不能宣布字节复制已解决；仍可能有其他复制或错误交付延迟 |
| 9 | 为什么栈里仍有_rt_memcpy_w？ | 继续查PSP、保存寄存器、反汇编和链接信息 | 找到没有被受控复制覆盖的前台访问 |
| 10 | FatFS是不是还直接读SDRAM？ | 恢复出f_write部分扇区缓存复制，源为SDRAM，目的为DTCM | 原来只控制BSP复制，遗漏了FatFS内部复制 |
| 11 | 怎样约束这条路径？ | 在FatFS入口前增加独立小块输入缓冲，先受控复制，再调用f_write | FatFS只拿内部RAM指针，原帧租约覆盖复制和同步调用 |

不同轮次的寄存器、地址和参数不能拼成一个原子现场。“后来能生成文件”也不能代替长期稳定性验证。

## 首次快照：错误在什么条件下被观察到

历史一组完整记录：

| 字段 | 值 |
| --- | --- |
| copy_consistent / copy_active | 1 / 1 |
| source | 0xD04B2B40，SDRAM |
| destination | 0x24000000，AXI |
| bytes | 0x400＝1024B |
| dma_lisr_before / dma_lisr_fault | 0x10 / 0x11 |
| dma_error | 2 |
| dma_cr / dma_fcr / dma_ndtr | 0x00865517 / 0x97 / 0x269 |
| dcmi_risr | 0 |
| sd_idmactrl | 0 |

这次复制开始前读取的LISR没有FEIF0，错误快照里新增FEIF0。状态支持“Storage复制附近观察到FE”，但没有测量FMC仲裁延迟或FIFO瞬时水位，不能直接断言是FMC导致硬件FIFO溢出。

后一次字节复制对照中，copy_active=0、source=0xD0480114、bytes=0x200，LISR仍从0x10到0x11，dma_error=2、IDMACTRL=0。它表明错误捕获不在已标记区间内，促使我继续找其他路径；既不能否认前一次现场，也不能证明字节复制有效。

**FE是DMA FIFO错误；只有确认DCMI的OVR等错误位，才有依据谈DCMI接收溢出。两者不能混用。**

## 调用栈不完整时，怎样继续找

历史调用栈下方曾出现Reset_Handler或地址0。我没有据此认定Storage真的从Reset_Handler调用过来，也没有直接判断栈溢出。

库复制会复用寄存器，异常发生时LR未必仍是可靠的C调用返回地址。于是结合PSP保存区、反汇编和Map核对，恢复出：

- R1源地址：0xD04F2A10，SDRAM。
- R0目的地址：0x20002058，DTCM文件工作区。
- 中断打断PC：0x08000736，处于库复制实现。
- 栈中保存的返回位置：0x0801EF87，对应f_write内部复制之后。

这些值只适用于当轮镜像，不是后续固件可复用的断点地址。

## 真正遗漏的是FatFS部分扇区路径

当时数据路径：

~~~text
JPEG SDRAM指针
    ↓
StorageFS_Write → f_write
    ├── 整扇区 → disk_write → BSP受控复制 → AXI → IDMA
    └── 部分扇区 → memcpy到FatFS文件缓存
~~~

FatFS处理部分扇区时有类似代码：

~~~c
memcpy(fp->buf + fp->fptr % SS(fs), wbuff, wcnt);
~~~

wbuff仍可能是原始JPEG SDRAM指针。只改BSP的memcpy，并没有覆盖更早发生的文件缓存复制。

后续边界改成：

~~~text
JPEG SDRAM，保持原帧租约
    ↓ 受控复制
独立内部RAM小块输入缓冲
    ↓
StorageFS_Write / FatFS
    ↓ 原有BSP AXI中转
SDMMC IDMA → SD卡
~~~

FatFS无论整扇区还是部分扇区，都只接触内部输入缓冲。输入复制必须在原帧有效期间完成，f_write返回前不能复用输入数据；SD失败也不能强行释放USB引用。

1KiB是字节复制对照当轮参数；后来的输入中转调整到8KiB。两轮不能写成同一个实验。没有为此修改HAL、关闭错误检测或伪造归还成功。

## 结论边界

已确认：

1. 首次故障来自Camera的DMA FE。
2. 至少一次FE捕获时SDMMC IDMA未使能。
3. BSP之外还存在FatFS部分扇区直接读取SDRAM的路径。
4. 已在FatFS入口前增加受控输入缓冲，用户随后能够生成并检查录像文件。

尚未确认：

- FMC仲裁、SDRAM时序、DMA服务裕量或CPU访问方式中，哪项是唯一硬件根因。
- 所有画面复杂度、并行负载和持续运行条件下是否都不再发生FE。
- 探针和断点对故障触发概率的影响。
- 停止重启、掉卡和错误恢复组合的完整实板复验。

因此，本次是“找到并修正遗漏访问路径”，不是“证明FMC冲突且彻底结案”。

## 面试60～90秒复述

> 我在相机和USB正常工作的基础上增加SD保存，结果Camera先发出FAULT，系统再协调停止其他链路。我开始怀疑SDMMC DMA与采集DMA竞争，但首次现场显示SDMMC IDMA尚未使能，这个解释不够。我确认被打断的是Storage的memcpy，又记录了源、目的、长度和复制前后的DMA标志。曾把受控复制改为逐字节，但仍报错，所以没有把它说成修复成功。继续结合异常栈和反汇编，才发现FatFS处理部分扇区时仍直接读取JPEG SDRAM，原先只控制BSP复制漏了一层。最后把受控输入边界前移到FatFS之前，同时保留帧租约。实际定位的是一条遗漏访问路径，FMC是否为唯一硬件原因和长期稳定性仍需继续验证。

## 面试追问

| 追问 | 回答重点 |
| --- | --- |
| 为什么不根据memcpy调用栈直接定罪？ | IRQ只说明打断位置，还需核对地址、任务和错误时机 |
| 为什么改一处memcpy不够？ | FatFS还有部分扇区缓存复制，要审计完整链路 |
| 为什么IDMA=0很重要？ | 排除“必须正在SDMMC IDMA传输才会触发”的必要条件 |
| 为什么不是直接写成DCMI溢出？ | 实际证据是DMA FE，属于不同外设错误 |
| 换了中转缓冲就能提前归还帧吗？ | 不能，所有仍可能读取原图的操作必须受原帧租约保护 |
