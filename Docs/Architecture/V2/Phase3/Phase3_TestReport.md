# Phase 3专项及回归报告

生产基线cbabe23；79个独立软件测试ID，O0/O2各执行一次，158/158 PASS。TestMatrix.csv逐项索引；results.json含原始返回值、失败行、完整事件trace和Source SHA256。测试真实链接Managed Lifecycle、Supervisor、Resolver及Media Resource生产C代码，使用Camera/USB/Storage可控Module与有界运输夹具；没有真实RTOS线程、硬件或文件操作。

主要场景：依赖启动、逆依赖停止、两类单消费者停止、双消费者退出、部分成功后补偿、停止失败固定Provider、未知部分状态BLOCKED、超时保留在途及迟到完成、未提交请求超时、submit/consume队列满、完整身份重复/冲突/旧消息、非法转换、Seal、旧资源未退休拒绝begin、补偿失败、首错冻结与延迟消费、计划不能隐藏活动依赖者、失败输出不变。ID56/71使用真实MR_VerifyExit验证端口未就绪/Receipt未退休的BUSY门禁；不将其解释为真实帧DMA安全证明。

8/8负对照有效：O0/O2各故意移除排空guard、完整回复身份校验、首错冻结、逆依赖停止顺序，原专项均以断言失败检出；编译/模拟器错误不计检出。变异副本只在output目录，无生产变异提交，不改历史断言。

| 回归 | 实际执行结果 | 范围 |
|---|---|---|
| Phase1 |250/250|原125 ID，O0/O2；包含新增C一起链接 |
| Phase2 Resource |112/112|原56 ID，O0/O2，无原测试修改 |
| C2.1.1 Replay |648/648|O0/O2原集合含长期循环 |
| C2 ABI |2/2|原布局probe，O0/O2 |
| C1 |419/419|原O0入口及旧队列基线轨迹比较 |
| C1.1 |24/24|原O0 reliability入口 |

所有原用例/历史FAIL/原results未改。Phase3运行器将本轮证据写独立Tests/V2Phase3及output/V2Phase3，不把原B1 37/39、Consumer42/44等改写成全绿，也未声称本轮完整重跑旧B1/B2/B3。入口和完整退出码见Evidence/*_commands.json；所有最终编译/链接命令退出0、无Warning/Error。

使用python Tests/V2Phase3/run_tests.py、run_negative.py、run_resource.py、run_regression.py、run_protocol.py、run_build.py重现。依赖现有本机ARMCC5/UV4、output/V2Phase1/python_deps中的Unicorn2.1.4/pyelftools0.33；不把缺依赖、未执行或编译异常当PASS。测试脚本自身不改变任务/工程配置。

NOT_RUN：真实任务消息交付/消费、真实Camera模块、USB/Storage业务、Frame/DMA读取者安全、实板故障/Abort及实际恢复、Phase4普通Stop接入。两个总体NOT_PROVEN与原72/32集合完全保留，不用158次模拟执行来关闭它们。
