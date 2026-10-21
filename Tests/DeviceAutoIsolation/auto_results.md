# CM4 Device 自动注册与初始化仿真

使用实际device.c、framework_init.c、DEVICE_EXPORT宏及真实链接段边界；模拟设备和生命周期回调，无硬件。
每个测试从全新仿真状态开始；NULL/重复导出项只在仿真镜像中注入，不修改生产Flash或源码。

| 测试 | 输入 | 预期 | 实际 | 结果 |
|---|---|---|---|---|
| 正常自动启动 | 导出a/b/c；auto_init=true/false/true；调用Framework_InitRuntime | 注册3个；只初始化a、c；后续等级继续 | 返回0；第二次/回调内操作返回0；注册3；回调次数2，顺序13；后续等级轨迹1456；a/b/c/extra状态=READY/UNINIT/READY/UNINIT | Pass |
| 全部手动初始化 | 3个auto_init均false | 注册3个，无初始化回调，启动成功 | 返回0；第二次/回调内操作返回0；注册3；回调次数0，顺序0；后续等级轨迹1456；a/b/c/extra状态=UNINIT/UNINIT/UNINIT/UNINIT | Pass |
| 空导出指针 | 测试注入：第二个导出项指针为NULL | 返回-1；保留第一个注册项；不初始化、不执行后续等级 | 返回-1；第二次/回调内操作返回0；注册1；回调次数0，顺序0；后续等级轨迹1；a/b/c/extra状态=UNINIT/UNINIT/UNINIT/UNINIT | Pass |
| 重复导出指针 | 测试注入：第二个导出项也指向a | 返回-3；链表仅一个a，无重复节点 | 返回-3；第二次/回调内操作返回0；注册1；回调次数0，顺序0；后续等级轨迹1；a/b/c/extra状态=UNINIT/UNINIT/UNINIT/UNINIT | Pass |
| 无效设备名称 | 第二个设备名称为空字符串 | 返回-1；注册中止，c未注册 | 返回-1；第二次/回调内操作返回0；注册1；回调次数0，顺序0；后续等级轨迹1；a/b/c/extra状态=UNINIT/UNINIT/UNINIT/UNINIT | Pass |
| 非法首次注册状态 | 第二个设备预设READY | 返回-4；注册中止，c未注册 | 返回-4；第二次/回调内操作返回0；注册1；回调次数0，顺序0；后续等级轨迹1；a/b/c/extra状态=UNINIT/READY/UNINIT/UNINIT | Pass |
| 跳过无回调的手动设备 | b.auto_init=false且lifecycle=NULL | b不初始化，不阻塞a、c或后续等级 | 返回0；第二次/回调内操作返回0；注册3；回调次数2，顺序13；后续等级轨迹1456；a/b/c/extra状态=READY/UNINIT/READY/UNINIT | Pass |
| 自动设备缺失回调 | b.auto_init=true且lifecycle=NULL | 返回-7；a已READY；b/c未初始化，后续等级停止 | 返回-7；第二次/回调内操作返回0；注册3；回调次数1，顺序1；后续等级轨迹1；a/b/c/extra状态=READY/UNINIT/UNINIT/UNINIT | Pass |
| 初始化回调失败 | c.init返回IO=-9 | 原样返回-9；a保留READY，c进入ERROR，后续等级停止 | 返回-9；第二次/回调内操作返回0；注册3；回调次数2，顺序13；后续等级轨迹1；a/b/c/extra状态=READY/UNINIT/ERROR/UNINIT | Pass |
| 重复RegisterAll | 连续调用两次Device_RegisterAll | 第二次返回-3；无链表损坏；非幂等 | 返回0；第二次/回调内操作返回-3；注册3；回调次数0，顺序0；后续等级轨迹0；a/b/c/extra状态=UNINIT/UNINIT/UNINIT/UNINIT | Pass |
| 重复InitAll | 初始化成功后再次调用Device_InitAll | 第二次返回-4；不会重复调用已READY设备回调 | 返回-4；第二次/回调内操作返回0；注册3；回调次数2，顺序13；后续等级轨迹0；a/b/c/extra状态=READY/UNINIT/READY/UNINIT | Pass |
| 提前手动初始化 | RegisterAll后手动Init(a)，再InitAll | 返回-4并中止；c仍未初始化 | 返回-4；第二次/回调内操作返回0；注册3；回调次数1，顺序1；后续等级轨迹0；a/b/c/extra状态=READY/UNINIT/UNINIT/UNINIT | Pass |
| 未注册先InitAll | 空Registry直接调用Device_InitAll | 返回0，无初始化；不会检查是否执行过RegisterAll | 返回0；第二次/回调内操作返回0；注册0；回调次数0，顺序0；后续等级轨迹0；a/b/c/extra状态=UNINIT/UNINIT/UNINIT/UNINIT | Pass |
| Init回调重入自身 | a.init第一次调用Device_Init(a) | 应拒绝重入，不得重复执行同一设备的初始化回调 | 返回0；第二次/回调内操作返回0；注册3；回调次数3，顺序113；后续等级轨迹1456；a/b/c/extra状态=READY/UNINIT/READY/UNINIT；失败断言行140；仿真异常=None | Fail |
| 初始化时追加设备 | a.init注册auto_init=true的extra | 当前实现在同轮访问新增extra；回调顺序a→c→extra | 返回0；第二次/回调内操作返回0；注册4；回调次数3，顺序134；后续等级轨迹1456；a/b/c/extra状态=READY/UNINIT/READY/READY | Pass |

14/15 Pass；其余逐项见缺陷分析。

等级轨迹1456表示PLATFORM及后续MEDIA/SERVICE/APPLICATION成功执行；轨迹1表示在BUS或DEVICE失败后中止。
注册和初始化失败采用Fail-fast但不回滚：先前注册设备和已完成初始化的设备保留。
重复调用的-3/-4是当前非幂等策略，不自动归为算法缺陷。
本报告不代表正式Target链接或上板验收。OLED驱动尚未完成，不在模拟设备中补写其硬件实现。
