# P8.1 实施报告

生产验证提交为 bdd999b1e8e25e08988a208197ef692b9ae56ff7，V1 对照为 a3cc70cbd7e55253c076fbf5f9241155e53c7e01，原 Phase 8.0 文档基线为 d9ca3c9。

- 657ac7e 新增 Configuration 下的 V1 Board Descriptor、四个 Profile、纯候选接口及软件专项。
- bdd999b 在 CM7 的既有 Native Runtime 分组登记 27 个原 V2 C、1 个新板级 C 和 28 个 H。链接器新增两个静态入口符号保留项，实际保留范围由引用链决定。

继续使用 Legacy 的 Reset → SystemInit → main → App_TasksCreate → Framework_InitRuntime → App_TasksRecordInitialization → Platform_RtosStart → ControlTask BOOT 路径。Initcall 和 Device Export 的输入对象及大小一致。Execution Manager、三路 V2 Endpoint 的 RunRound 和 Control Service 的 Process 未进入加载区。未添加运行入口、任务或状态实例。

静态装配、ABI、回归、双核编译和静态资源预算均通过。V1 对照和 P8.1 的断电冷启动、PA15/U10、OV5640 ID、UVC 出图须实板分别确认，均为 NOT_RUN。真实 Grant → Frame Lease 与 Stable Admission Closure 保持 NOT_PROVEN。本轮停在 P8.1。
