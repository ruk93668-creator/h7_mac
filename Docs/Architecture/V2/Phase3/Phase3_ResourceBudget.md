# Phase 3 ARMCC5资源预算（实测与未验证分开）

ARMCC5 5.06u7 build960，C99 Cortex-M7.fp.sp；新增3个生产C单元实际编译、链接、Unicorn执行；不是Host sizeof推算。对象表/ARM运行sizeof见Tests/V2Phase3/resources.json。没有更改原52B AppMessage、56B LifecycleMessage或Phase1/2布局；原ABI probe两次执行通过。

| ARM对象 | bytes | 所有者 |
|---|---:|---|
| LCContext |112|每模块唯一owner |
| EMContext |1212|唯一Supervisor owner，内含32步骤计划、32补偿条目和16端点 |
| EMPlan |392|Composition调用者输出，Load再复制至EMContext |
| LCCompletion |28|本地值，不是新wire格式 |
| LCRequest |24|本地值，不是AppMessage布局 |
| V2LifecycleOps |16|const Ops及port生命周期由模块装配者保证 |
| V2ExecId |16|独立执行身份，不与G/R/H混用 |
| EMEndpoint |20|运输适配，不含TCB/Frame |

一个EM+三个LC实例需1548B调用者存储，另计392B外部计划、模块Ops/运输适配状态及已有Composition存储。当前没有生产全局实例，新增库RW/ZI为0；不能把这解释为集成不需要RAM。帧/SDRAM缓冲增量0，未创建第二个Pool/物理引用计数、消息队列或任务。

## O0

生产对象RO合计4448B，RW/ZI=0。选定测试路径SP最大下降1496B；Maximum Stack Usage =       1496 bytes + Unknown(Untraceable Function Pointers)。这是包含测试调用者的链接/模拟路径，间接Ops仍Unknown，不是实际任务high-water或WCET。

- LC_Process：函数自身栈16B，链接器已知最大深度24B（回调路径另计）。
- EM_Process：函数自身栈56B，链接器已知最大深度96B（回调路径另计）。
- EM_BuildPlan：函数自身栈768B，链接器已知最大深度936B（回调路径另计）。
## O2

生产对象RO合计3530B，RW/ZI=0。选定测试路径SP最大下降1464B；Maximum Stack Usage =       1464 bytes + Unknown(Untraceable Function Pointers)。这是包含测试调用者的链接/模拟路径，间接Ops仍Unknown，不是实际任务high-water或WCET。

- LC_Process：函数自身栈24B，链接器已知最大深度32B（回调路径另计）。
- EM_Process：函数自身栈64B，链接器已知最大深度104B（回调路径另计）。
- EM_BuildPlan：函数自身栈760B，链接器已知最大深度880B（回调路径另计）。

计划构建局部EMPlan、Resolver Scratch及验证输出导致有界栈开销。构建应在装配/配置owner串行调用，不放ISR或媒体热路径；每Source/模块单owner串行，不把busy当多任务锁。执行每轮至多一个submit/read/consume回调，计划最多32正向步骤+32补偿操作；纯构建的扫描上限受16模块/既有Resolver限制。不能把循环上限当CPU实测周期。

原Keil完整CM7：Code137690 RO3114 RW1340 ZI100932；CM4：Code26350 RO1438 RW936 ZI3576。新V2库未登记旧Target，因此这些镜像未激活Execution，不能将原镜像无RAM变化称为集成通过。独立ARMCC目标提供新库真实RO/sizeof/栈证据。完整MAP/HTM/AXF/HEX（原Target已有的）保留output/V2Phase3，记录SHA256；没有覆盖原output或Keil设置。实际Camera/USB/Storage任务栈、ISR嵌套、调度延迟、总线争用、运行FPS/WCET均NOT_RUN，禁止据本报告调整栈/优先级。
