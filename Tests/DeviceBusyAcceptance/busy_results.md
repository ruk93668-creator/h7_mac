# CM4 Device BUSY 定向验收

直接使用当前CM4 Device和Framework Initcall源码；静态设备采用真实DEVICE_EXPORT宏；时间和硬件不参与。
每个测试使用全新仿真状态，恢复ELF初始化数据，模拟生命周期回调检查BUSY与设备指针。

| 测试 | 输入 | 预期 | 实际 | 结果 |
|---|---|---|---|---|
| 正常状态转换 | Register→Init→Start→Stop | UNINIT→BUSY→READY→BUSY→RUNNING→BUSY→READY；回调各1次 | 返回0；嵌套Init/Start/Stop=0/0/0；回调次数=1/1/1；回调见BUSY=3；manual/automatic=READY/UNINIT；Registry=2 | Pass |
| Init回调重入及交叉调用 | Init回调内调用Init/Start/Stop | 三个嵌套调用均返回BUSY=-5，无额外回调，外层成功后READY | 返回0；嵌套Init/Start/Stop=-5/-5/-5；回调次数=1/0/0；回调见BUSY=1；manual/automatic=READY/UNINIT；Registry=2 | Pass |
| Start回调重入及交叉调用 | Start回调内调用Init/Start/Stop | 三个嵌套调用均返回-5，无额外回调，外层成功后RUNNING | 返回0；嵌套Init/Start/Stop=-5/-5/-5；回调次数=1/1/0；回调见BUSY=2；manual/automatic=RUNNING/UNINIT；Registry=2 | Pass |
| Stop回调重入及交叉调用 | Stop回调内调用Init/Start/Stop | 三个嵌套调用均返回-5，无额外回调，外层成功后READY | 返回0；嵌套Init/Start/Stop=-5/-5/-5；回调次数=1/1/1；回调见BUSY=3；manual/automatic=READY/UNINIT；Registry=2 | Pass |
| 已有BUSY状态拒绝操作 | 已注册对象设为BUSY，调用Init/Start/Stop | 三个返回-5；保留BUSY，不执行回调 | 返回0；嵌套Init/Start/Stop=-5/-5/-5；回调次数=0/0/0；回调见BUSY=0；manual/automatic=BUSY/UNINIT；Registry=2 | Pass |
| 缺失Init Ops | UNINIT时使用NULL lifecycle和NULL init | 返回NOT_SUPPORTED=-7，保持UNINIT | 返回-7；嵌套Init/Start/Stop=0/0/0；回调次数=0/0/0；回调见BUSY=0；manual/automatic=UNINIT/UNINIT；Registry=2 | Pass |
| 缺失Start Ops | READY时使用NULL lifecycle和NULL start | 返回-7，保持READY | 返回-7；嵌套Init/Start/Stop=0/0/0；回调次数=1/0/0；回调见BUSY=1；manual/automatic=READY/UNINIT；Registry=2 | Pass |
| 缺失Stop Ops | RUNNING时使用NULL lifecycle和NULL stop | 返回-7，保持RUNNING | 返回-7；嵌套Init/Start/Stop=0/0/0；回调次数=1/1/0；回调见BUSY=2；manual/automatic=RUNNING/UNINIT；Registry=2 | Pass |
| 静态导出自动注册 | 两个真实DEVICE_EXPORT项；RegisterAll后Find | 均进入Registry，查找返回原对象，链表正确，无Init回调 | 返回0；嵌套Init/Start/Stop=0/0/0；回调次数=0/0/0；回调见BUSY=0；manual/automatic=UNINIT/UNINIT；Registry=2 | Pass |
| auto_init策略 | manual=false，automatic=true；Device_InitAll | manual只注册、保持UNINIT；automatic执行1次Init并READY | 返回0；嵌套Init/Start/Stop=0/0/0；回调次数=1/0/0；回调见BUSY=1；manual/automatic=UNINIT/READY；Registry=2 | Pass |
| 真实Initcall链 | 调用实际Framework_InitRuntime | BUS注册，DEVICE按策略初始化，后续等级继续 | 返回0；嵌套Init/Start/Stop=0/0/0；回调次数=1/0/0；回调见BUSY=1；manual/automatic=UNINIT/READY；Registry=2 | Pass |
| Init回调失败 | Init回调返回IO=-9 | 回调期间BUSY，原样返回-9，退出BUSY并进入ERROR | 返回-9；嵌套Init/Start/Stop=0/0/0；回调次数=1/0/0；回调见BUSY=1；manual/automatic=ERROR/UNINIT；Registry=2 | Pass |
| Start回调失败 | Start回调返回IO=-9 | 原样返回-9，退出BUSY并进入ERROR | 返回-9；嵌套Init/Start/Stop=0/0/0；回调次数=1/1/0；回调见BUSY=2；manual/automatic=ERROR/UNINIT；Registry=2 | Pass |
| Stop回调失败 | Stop回调返回IO=-9 | 原样返回-9，退出BUSY并进入ERROR | 返回-9；嵌套Init/Start/Stop=0/0/0；回调次数=1/1/1；回调见BUSY=3；manual/automatic=ERROR/UNINIT；Registry=2 | Pass |

结果：14/14 Pass。

所有Init/Start/Stop回调内部的3×3嵌套操作均检查返回-5、BUSY不被覆盖及回调计数不增加。
两个静态导出项的Flash表大小8字节，指针与实际设备对象地址一致；注册后检查Registry双向链表与Find对象身份。
全Target构建日志及Warning另见cm4_keil.log；本报告不代表上板并发测试。
