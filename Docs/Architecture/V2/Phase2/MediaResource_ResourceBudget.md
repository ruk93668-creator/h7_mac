# 资源预算计划（PROPOSED，未经编译测量）

## 已有证据与不能重复分配的存储

501330a的ARM测量：Composition scratch696B、候选276B，带issues工作集1020B、最大输入合计2220B；对象RO O0/O2为10088/8144B，RW/ZI0。Mock栈556/580B并非未来Resource路径上界，未重测。

camera_capture.c:11–40定义Raw七slot，每slot容量640×480×2=614400B，总保留4300800B（4.1015625MiB）；实际帧长度依配置，160×120 YUY2为38400B，但不能据此缩减物理保留区。Snapshot的63KiB窗口与Raw地址复用，不作为额外独立池相加。JPEG头定义512KiB FIFO和两个8KiB drop块，共540672B，64个FIFO块、7个ready/loan上限；地址与Raw区不重叠。现有两区域合计4841472B，不是V2新增申请量。原代码有重叠/窗口静态检查，不允许为了统一API再建一套相同Frame Pool。

## 元数据容量与测量方式

候选仅为讨论预算：2个真实消费者；每方向1个当前交接；每USB通道1个未消费Terminal Receipt；沿用7个JPEG Loan；Storage slot容量必须从实际装配读取，不将本轮讨论值写死。消费者表未来参数化，但后端不支持第三owner则拒绝。

身份草案G为4×uint32（session/consumer/generation/acquire），R独立request；M至少backend kind/session/loan-or-slot/generation；H至少流epoch/lease/交接代次。若以G16+H12+M16+状态/结果12+原生句柄8作一个64B关联槽，7槽448B；独立T含身份/结果/消费标志暂按64B；每consumer关闭记录G16+R4+关闭代次4+标志8=32B，两路64B，合计候选576B。**这不是sizeof实测、完整结构设计或新增RAM承诺**，可能还需异步DMA记录/对齐，禁止把省略项当0。关联只存凭据与协议阶段，不存第二份物理引用计数。

精确增量必须在接口批准后：ARMCC5 sizeof/offsetof→对象RW/ZI→实际Map→扣除被替换元数据→与501330a同Target重编对比。纯常量端口表计Flash，复制视图/Receipt计RAM，物理SDRAM分别记保留与活跃。新增任务/队列/栈当前预算0；若设计需要则另审，不自动沿用旧协议对象再加同样四方向副本。

Raw零拷贝独立多owner若需新增slot账本/复制缓存，内存与CPU都UNRESOLVED；优先保留现Backend能力并拒绝不支持组合。复制方案预算至少Ncopy×对齐后的帧长，计实际源读取带宽和完成时点，不能借缩小临时buffer宣称无竞争。JPEG仍按原块/双span管理，不能改成整帧memcpy作为“接口统一”。

## 栈、CPU、DMA验证门禁

现任务Camera6144B、USB/Storage各8192B，保持原配置。Resource owner不得在栈上分配Frame或大临时池；短临界区持有时间、IRQ延迟、每次核验扫描上限、缓存维护及DWT必须实测。完整调用图需纳入真实纯Validator和Backend回调，间接调用Unknown不得忽略。真实stack high-water需故障/双消费者压力下采样；CPU/WCET、DMA退休均NOT_PROVEN。

Phase2的目标是实现资源权威端口及必要元数据，物理后端优先复用；本轮仅文档，不实际分配。以上属于冻结Phase2设计材料，不改变Phase0～10路线。
