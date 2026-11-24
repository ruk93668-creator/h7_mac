# Q1.5 Phase 0 兼容性与依赖矩阵

冻结 main 为 `f0ce45fd9ab7250b1e22260e087d606ee5ed9ecb`，QSPI 输入为 `fdb45718ec60db0c4d970d02cad70fa99662dd78`。本轮仅源码分析。Git 对比证明分支的 Application、Media、Platform、Framework、CM4 和 Common 与 main 一致，因此这些本地文件可以作为冻结主线证据。正式生产差异仅新增 BSP 两文件、启用 CM7 HAL QSPI 及 Keil 源文件登记。未读取其他分支作为主线替代物。

## 外设和所有权

|资源|主线现状及依据|QSPI 影响与设计结论|
|---|---|---|
|PF6–PF10 / PG6|SDRAM 使用 PF0–5、PF11–15，PG0–2/4/5/8/15；DCMI 使用 PG10 等其他脚。见 [SDRAM:48](../../CM7/BSP/bsp_sdram.c#L48)、[DCMI:241](../../CM7/BSP/bsp_dcmi.c#L241)|QSPI 六脚未与以上脚位重叠。共享 GPIOF/G 寄存器和端口门，必须保留位级保存/恢复，禁止整个 GPIO 端口复位或关门。源级不冲突，运行并发仍须验证|
|SDMMC / OV5640 / USB|SDMMC PC8–12/PD2 与 PB 卡检测；Camera SCCB PB8/9、PH15 reset；DCMI PC6/7、PG10、PH12、PE4–6、PB6/7、PA4/6；USB PA11/12。见 [SDMMC:366](../../CM7/BSP/bsp_sdmmc.c#L366)、[Camera:9](../../CM7/BSP/bsp_camera.c#L9)、[USB:579](../../CM7/BSP/bsp_usb.c#L579)|与 QSPI 引脚无直接重叠。只读共存测试记录其他外设时钟、GPIO 和诊断，判断清理/恢复是否误改共享寄存器|
|时钟|主线 SYSCLK 配方 480 MHz，HCLK 240 MHz。相机 XCLK 与 SDMMC 使用 PLL1Q；FMC 使用 PLL2R；USB 使用 HSI48。见 [系统时钟:233](../../CM7/Core/Src/main.c#L233)、[SDMMC:717](../../CM7/BSP/bsp_sdmmc.c#L717)、[FMC:93](../../CM7/BSP/bsp_sdram.c#L93)、[USB:777](../../CM7/BSP/bsp_usb.c#L777)|QSPI 选 D1HCLK、分频 2，配置目标 120 MHz；不重配共享 PLL。动态 HCLK 改变将触发配置契约错误，禁止将降频结果记为 120 MHz PASS|
|QUADSPI 所有权|main 无 QSPI 使用者，CM4 未配置 QSPI。CM4 SCCB/OLED 使用 PB10/11。见 [CM4 IIC:57](../../CM4/BSP/bsp_iic.c#L57)|BSP 私有 HAL 句柄、原子操作权只有 M7 内部意义，无法替代跨核锁。维持 M7 独占，CM4 不调用、不改引脚/mux/gate；不借 HSEM 0 实现 QSPI 并发|
|双核启动与 IPC|M7 在 HSEM 0 释放前准备共享引脚/早期 IPC；两核保留各自正式启动流程。见 [M7:153](../../CM7/Core/Src/main.c#L153)、[M4:86](../../CM4/Core/Src/main.c#L86)|正式共存不可沿用 H2 的停驻 CM4 替代生产应用。保留 IPC ABI、早期启动和 CM4 正常框架；H2 成功只证明独立候选|
|MPU / SDRAM / DMA|Region1 为 SRAM4 4 KiB，Region2 为 0xD0000000 起 8 MiB 非缓存 SDRAM，QSPI 未映射。见 [MPU:77](../../CM7/Core/Src/main.c#L77)、[MPU:302](../../CM7/Core/Src/main.c#L302)|继续间接轮询，只用内部静态缓冲，不新建 MPU 区域、不使用 Memory Mapped/XIP/DMA。保留相机帧 Lease/FIFO、SD 中转和 USB Reader 的缓冲所有权|
|初始化|Framework 按 PLATFORM→BUS→DEVICE→MEDIA→SERVICE→APPLICATION 执行。SDRAM 为 BUS；SD 块层 PLATFORM 准备，StorageFS SERVICE 初始化不挂载。见 [Framework:39](../../CM7/Framework/framework_init.c#L39)、[SD:273](../../CM7/Platform/sd_block.c#L273)、[FS:782](../../CM7/Platform/storage_fs.c#L782)|不新增执行阻塞识别的 Initcall。QSPI 初次 Init 在 BOOT 成功后的合法任务中显式触发。与业务准备分离，识别失败可隔离 QSPI，不能自动重启其他模块|
|FAULT / Cleanup / Recover|读失败保存首错后清理，失败保留资源；Recover 重新验证资源/ID/状态。见 [Cleanup:272](../../CM7/BSP/bsp_qspi.c#L272)、[ReadFinish:743](../../CM7/BSP/bsp_qspi.c#L743)、[Recover:504](../../CM7/BSP/bsp_qspi.c#L504)|其他模块不得再次改 QSPI 六脚/mux；显式维护路径才允许恢复，不在 Camera/USB 回调恢复。不自动重试 Init/Recover。异常导致尚未证明释放时停止 QSPI 访问而非复位 GPIO 或共享 PLL|
|FatFS 与业务数据|StorageFS 为唯一卷所有者，有固定文件槽和 8 KiB 输入中转；SD 块设备租约由 Platform 管理。见 [FS:19](../../CM7/Platform/storage_fs.c#L19)|QSPI 不接 FatFS、不增加存储卷或参数分区；只读测试请求不借用 SD/相机缓冲。现有写 API 的公共策略为空，不能通过普通 Setter 放开|
|ApplicationV3|V3 是独立候选，本分支 main 底座仍为原 Application|不主动合并 V3，不把其服务、任务和回归当成本轮底座。V3 获准进入 main 后重新建立集成基线并复核矩阵|

## FreeRTOS 与中断预算

|任务 / 中断|冻结主线配置|对 QSPI 调用的限制|
|---|---|---|
|USB task|2048 words，优先级 4，见 [usb_task:15](../../CM7/Application/Tasks/usb_task.c#L15)|不在流处理关键任务执行同步 QSPI|
|Camera task|1536 words，优先级 3，见 [camera_task:11](../../CM7/Application/Tasks/camera_task.c#L11)|不在逐帧处理、持有 Frame Lease 或控制切换关键段执行|
|Control task|768 words，优先级 2，见 [control_task:11](../../CM7/Application/Tasks/control_task.c#L11)|不将最长 5.2 秒 Init/Recover 放到控制响应热路径|
|Storage / IPC|2048 / 512 words，均优先级 1，见 [storage_task:9](../../CM7/Application/Tasks/storage_task.c#L9)、[ipc_task:10](../../CM7/Application/Tasks/ipc_task.c#L10)|相同优先级轮转下仍可能延迟处理，不保证 SD 响应；测试低占空比并显式让出 CPU|
|TIM6 / DCMI及DMA / USB / SDMMC|NVIC 优先级依次 4 / 5 / 6 / 7。见 [TIM6:4](../../CM7/Platform/platform_hal_timebase.c#L4)、[DCMI:14](../../CM7/BSP/bsp_dcmi.c#L14)、[USB:12](../../CM7/BSP/bsp_usb.c#L12)、[SDMMC:491](../../CM7/BSP/bsp_sdmmc.c#L491)|数值较小者抢占优先。BSP 只在取放操作权时短暂 PRIMASK，轮询期间不长时间关中断，但 HAL 忙等消耗 CPU/总线；实时性需要共存数据，无法由可抢占性直接推断通过|
|FreeRTOS 内核|1 kHz、抢占、同级轮转，静态分配、无 Mutex/软件 timer，MAX_SYSCALL IRQ=5，见 [配置:18](../../CM7/Platform/FreeRTOSConfig.h#L18)|ISR、PRIMASK/FAULTMASK、会屏蔽 TIM6 的 BASEPRI 环境不能调用；禁止持调度锁/临界区做轮询。TIM6 必须全过程前进，BSP 本身不强制验证 FreeRTOS 调度器状态，由调用者负责合法任务契约|

读取最大公开总预算为 60,000 ms，内部 256 B 分段共用总期限；单次 HAL 最多 100 ms；Init/Recover 总预算 5,200 ms，实际 HAL 初始化默认 5,000 ms。写入限时、轮询和 CPU 竞争也存在，但 Q1.5-A/B 仅运行只读路径。详见 [BSP:23](../../CM7/BSP/bsp_qspi.c#L23)、[Read:827](../../CM7/BSP/bsp_qspi.c#L827)及 [API](../../CM7/BSP/bsp_qspi.h)。

建议当前只保留 BSP，不新建产品 QSPI Service。尚无真实业务消费者需要请求队列、生命周期或仲裁层。测试路径使用隔离静态低优先级任务，初始建议 768 words / 优先级 1，最多一次 256 B 读取、总调用预算建议 20 ms、调用间显式延时至少 10 ms；这些是待 A 验证的试验参数，不能当作实时性证明。Init/Recover 仅在流未运行的维护时点执行。若后续出现多个业务消费者或稳定时限需求，应另行评审服务层和调度设计。

## Keil 与测试代码边界

Git 提交树的 QSPI 工程新增 HAL QSPI、bsp_qspi.c/.h 及 `Tests/QspiFlashQ1/hardware/m7_project_test.c`。最后一项注册 Initcall 并创建测试任务，因此不能原样移植该工程到产品。BSP 文件头还有 H1、SFDP、Quad ID 的测试宏及 Tests include。A 阶段需在批准白名单内迁移测试构建适配，保持默认生产行为和原测试可复现，并证明所有原有 API、诊断 ABI 和写门禁不变。当前本地 .uvprojx/.uvoptx 有用户修改，不能用提交树覆盖它们。见 [测试入口:117](../../Tests/QspiFlashQ1/hardware/m7_project_test.c#L117)、[BSP:7](../../CM7/BSP/bsp_qspi.c#L7)。
