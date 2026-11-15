# ID17 根因与准入边界复核

基线：8a83591b4baa0a850ccdfb269b68d752af5bee81。现有分支codex/stage-C2-2B-B2B6-v1-compat。**生产CameraTask串行准入通过，未发现需要修改Qualification核心的生产路径缺陷；本轮不改生产代码。**

## A：Core同轮批量冷QUERY

直接复用原`Tests/ConsumerIntegrationC2_2B/consumer_test.c:106` ID17。两个Begin之后，同一Round先Flush两路请求，再执行Core Process。`app_camera_qualification.c:229`的Qual_OtherWork将另一Rx尚未终结/回复未入队视作未稳定工作；首次会话分配在266–281行受此门控。

第一个QUERY遇到另一条未完成QUERY而BUSY；第二个QUERY又遇到第一个尚未成功入队的终结回复而BUSY。都没有分配Session或启动资源。随后两路收到固定BUSY回复，Consumer进入FAILED。缓存的BUSY是不可变事务结果，不能把相同请求重传当作新成功请求。

O0/O2实际均为：session_id=0、starts=0、两路phase=FAILED(6)、result=BUSY(-5)、granted=0、资格INACTIVE(0)、final_reply_enqueued=[1,1]。原断言仍FAIL，return/failure_line=114。独立验收分类为EXPECTED_REJECT，不把原Consumer 42/44改写为44/44。该Core批量入口边界与现有CameraTask逐条准入不同，不能泛化为所有调用方式都支持同轮批量初始化。

## B：真实CameraTask串行准入

直接复用`camera_task_test.c:32` ID5，编译真实camera_task.c和app_camera.c。`camera_task.c:253–264`使用单个if接收，并对每条实际接收提交一次RuntimeEvent_Post；没有while批量登记。先执行Qualification Process、再进行既有Media/Bridge/Finish和回复Flush，下一轮才接下一条。

| 轮次 | Task处理前队列数 | 实际请求 | 本轮Session | 本轮新启动 | 自唤醒提交增量 |
|---|---|---|---|---|---|
| 0 | 2 | USB QUERY，session=0/request=1/consumer=1/gen=1 | 1建立 | 0 | 1 |
| 1 | 2 | Storage QUERY，session=0/request=1/consumer=2/gen=1 | 1复用 | 0 | 1 |
| 2 | 2 | USB ACQUIRE，session=1/request=2/consumer=1/gen=1 | 1 | 1 | 1 |
| 3 | 1 | Storage ACQUIRE，session=1/request=2/consumer=2/gen=1 | 1 | 0，复用运行资源 | 1 |
| 4–7 | 0 | 无 | 1 | 0 | 0 |

O0/O2均每轮至多1条；最终USB/Storage Consumer均GRANTED(3)、grant=1、Session同为1、结果OK；两路资格ACTIVE(2)，两路final_reply_enqueued=1。Device_Start=1、CameraCapture_StartStream=1、CameraCapture_Start=0，自唤醒提交4次，对应实际处理4条请求。

## 证据及适用边界

[逐轮与身份结果](results.json)，[原始事件](ProbeEvidence/task_O0.json.gz)、[O2原始事件](ProbeEvidence/task_O2.json.gz)，负向Core原始数据也独立保存。新增两个probe只include既有测试并读取编译器布局，原场景、断言和函数返回值未修改。观测轮次依据跨翻译单元的Consumer Flush/Process边界，避免O2内联后只靠函数入口计数失真；硬件入口同时以原fixture Trace代码22/25计数。

ID5既有fixture在Camera所有者合法上下文准备资格，并设置自身Task ready测试前置；实际生产Process/ReceiveLifecycle及Qualification、Consumer、Tx/Rx/Replay逻辑均被编译。调度、运输、Device/Media为既有测试桩；自唤醒证明的是Post提交与消息处理对应关系，下一轮由测试显式调度，**不等于真实FreeRTOS调度、通知合并或实板中断时序已验证**。硬件项目均NOT_RUN。

结论：ID17在Core同轮批量调用下仍拒绝；在已确认的现有CameraTask串行准入下，两路同时冷QUERY可安全获权，软件验收问题可按此边界关闭。不修改Core、协议或Replay，也不新增自动重试策略。
