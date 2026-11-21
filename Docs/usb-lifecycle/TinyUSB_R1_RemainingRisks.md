# TinyUSB R1剩余风险与最小接口缺口

BLOCKED / 高风险：R1未通过。新DWC2入口目前故障封锁而非完成取消，旧硬件无界等待仍可触发。禁止硬件启用。

1. UNRESOLVED：真正非零Bulk SUCCESS必须同时证明EPDISD/XFRC退休、PIO不再读写旧源、共享RX中不存在会写旧Buffer的状态。不能仅在调用现场读EPENA或立即清中断。现单次同步新API不能凭返回FAILED补齐该证明。
2. UNRESOLVED：最小后续接口需要保留TinyUSB现有DCD队列内的有界进度/完成事实与错误原因，不能用UNSUPPORTED表达已启动失败。当前没有该成功事件到应用资源退休的完整契约，不实施猜测序列。
3. UNRESOLVED：旧STALL/close_all/ISO/EP0 SETUP仍有void调用者会继续执行。需同一真实结果传播扩展覆盖它们和其Core/Class调用者；只改abort不能满足全生命周期标准。本轮不声称这些已安全。
4. UNRESOLVED：Flush与AHBIDL/CSRST等待需要真实单调时基及ISR可用性核验，或有证据的执行上界。没有合格时间提供者时不得命名为硬件超时；旧循环仍未改。
5. UNRESOLVED：终态Core/DCD fault锁存没有恢复API；GINTMSK=0只是保留故障、避免中断风暴，不证明硬件静止。不得清故障后重用旧Buffer或自动释放RAW。该选择牺牲整个控制器继续服务能力，不是生产恢复方案。
6. NOT_TESTED：H747 PIO ZLP时序仍独立门禁，未修改；FreeRTOS mutex/旧tick接口仍阻塞完整新固件构建。

已授权允许最小DCD/Core扩展；本报告不重复申请原有授权，也不以更换栈/旧UDLM/Watchdog规避。需要审核实际部分改动及上述证明缺口，才能定义下一轮完整实现和成功验收。所有实板项目NOT_TESTED。
