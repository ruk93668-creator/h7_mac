# C1 Camera消费者生命周期协议基础

## 阶段边界与业务选择

基线：main@35b4fb7f5c39f7339b0bf0aa2a68594fd9748e7f。
开发分支：codex/stage-C1-lifecycle-protocol。

C0已确认的后续规则：
1. USB Reset/Disconnect/Suspend/Resume不自动释放Camera资格，明确USB STOP才停止业务。
2. NONE改为按需采集，TEST不申请Camera。
3. Storage文件收尾失败但入口关闭、原始帧和交接全部安全结束时可释放资格，文件系统故障独立保留。

**C1只提供表达这些规则所需的协议，不启用这些业务变化。** 当前NONE冷启动、Control串联启动、USB和Storage旧START/STOP路径继续运行；消费者资格、会话生成、硬件启停、Shutdown/Restart留待C2及后续审核。新协议通道在Framework初始化阶段准备，但当前业务Task不会发送或消费它们。

## 消息与内存决策

正式基线AppMessage_t为52B。若按C0初稿追加32B，旧结构将增为84B，旧5×4项队列单独增加640B，还会影响现有消息局部变量、业务上下文及消息复制。

实施前已报告此影响，改用独立AppLifecycleMessage_t（56B）和独立API，旧结构、枚举编号、队列深度及52B复制保持原样。不是新建通用事件框架。

新3条通道各4项：控制块216B＋消息区672B＋句柄12B＝900B，实际含对齐的总RAM增量904B。没有改变任务栈和原队列容量。

## 固定布局

每个字段为32位整数，结构4字节自然对齐，不包含指针、RTOS句柄或枚举对象。

| 偏移 | 字段 | 定义 |
|---:|---|---|
| 0 | version | 当前1，未知版本拒绝 |
| 4 | kind | REQUEST=1，REPLY=2 |
| 8 | operation | 查询1、申请2、释放3、STOP_IF_IDLE4、Camera要求释放5、故障通知6 |
| 12 | source | Camera=1、USB=2、Storage=3；不是RuntimeEventTarget_t的直接数值 |
| 16 | target | 同上，按唯一接收角色路由 |
| 20 | identity.session_id | 非零Camera资源会话；只允许查询请求及查询失败回复为零 |
| 24 | identity.request_id | 事务发起方分配，回复回显，队列满沿用 |
| 28 | identity.consumer_id | USB=1、Storage=2，固定消费者身份 |
| 32 | identity.consumer_generation | 非零START尝试代次，由未来业务所有者推进 |
| 36 | related_request_id | 自主事务为零；仅RELEASE可关联Camera发出的释放请求 |
| 40 | completion_flags | 独立完成条件，不是一个STOPPED |
| 44 | result | 本协议事务结果；0成功，负数失败 |
| 48 | business_result | 业务或文件封尾原结果 |
| 52 | cleanup_result | 文件清理、卷等独立结果 |

SESSION_QUERY成功回复可返回新会话；其他回复必须匹配原会话。请求、来源、操作、consumer、generation、关联编号全部匹配后才允许终结待决请求。

此身份与现有stream_epoch、lease_id、Bridge帧请求command_id、Storage录像session严格分离。C1不分配真实Camera会话，只提供单所有者的有界批量ID分配工具。编号耗尽返回NO_RESOURCE，不修改计数和输出；不清零回绕。未来业务必须在授权前为释放事务预留编号。

## 完成条件

0x01入口关闭；0x02本地工作静止；0x04帧归还；0x08文件正常封尾；0x10文件关闭；0x20卷释放；0x40资格释放；0x80Camera硬件停止。

- 未知位拒绝。
- 资格释放只能由Camera的成功RELEASE回复声明。
- 硬件停止只能由Camera的成功STOP_IF_IDLE回复声明。
- 消费者可以同时报告FRAMES_DRAINED与business_result失败，不把文件失败清成正常完成。
- 协议校验只检查声明权限，**不证明实际硬件/文件/帧已经完成**。未来Camera必须依据真实账本核验。

## 通道与权限

新增独立枚举AppLifecycleChannel_t：Camera=0、USB=1、Storage=2。旧AppChannel_t及其五条通道完全不变。

- QUERY/ACQUIRE/RELEASE/STOP_IF_IDLE请求由所属消费者发给Camera，回复反向。
- RELEASE_REQUEST/FAULT_NOTICE请求由Camera发给所属消费者，回复反向。
- 新API同时校验协议字段、目标通道和App_Events登记的真实当前任务。
- Control、IPC不能冒充消费者；C1没有把Camera管理员Shutdown命令接入新通道，后续另行审查管理员入口。
- 只允许普通任务、调度器RUNNING且PRIMASK/BASEPRI为零时收发；初始化只允许调度前执行一次。
- 任一创建失败，新通道全部保持不可用，不重复复用部分StaticQueue_t；旧队列单独准备且不被重置。

Camera收件箱取消息后同时唤醒USB和Storage；消费者取消息后唤醒Camera。通知失败不撤销已经成功的队列提交，不要求重复入队。

## 待发与重复请求

AppLifecyclePending_t由单一任务持有，初始必须清零：

```text
EMPTY → Stage → READY
READY → Flush失败 → READY（完整原文不变）
READY → 请求入队成功 → WAIT_REPLY → 匹配回复 → COMPLETE
READY → 回复入队成功 → COMMITTED
COMPLETE / COMMITTED → 显式Clear → EMPTY
READY请求 → CancelReady → EMPTY（尚未提交，不释放业务资源）
```

回调期间SENDING阻止同一记录重入。已经入队的请求不会由Flush再次入队；超时不是Clear授权，WAIT_REPLY没有强清接口。COMMITTED仅表示回复入队，不表示接收方已经处理，更不表示资源已释放。

AppLifecycleReplay_t记录请求原文与终结回复：相同请求返回ALREADY_EXISTS；同键不同内容返回PROTOCOL；其他事务不能覆盖占用记录；终结失败不能被后来的成功覆盖。读取回复无副作用。

**C1.1已封闭ReplayRetire的成功路径。** 空指针返回INVALID_ARG；未占用或未完成返回BUSY；已占用且完成返回NOT_SUPPORTED。所有情况均不清除记录。completed、回复入队成功和超时都不是旧请求失效证明，缓存耗尽后拒绝新请求，不能自动淘汰或手工清零复用。后续必须先审核可验证的失效边界，再设计安全退休规则。本阶段没有实现tombstone、水位、完整会话表或业务幂等执行器。

CancelReady只允许取消READY状态的REQUEST；EMPTY、SENDING、WAIT_REPLY、COMPLETE、COMMITTED及READY回复均返回INVALID_STATE且不改变原记录。取消仅清除尚未提交的协议待发记录，不分配新身份、不发送消息、不释放业务资源。重复取消同样拒绝；取消后如显式重新Stage同一请求，只产生一次真实提交。

## 接口与依赖

app_camera_lifecycle.c/.h为普通C协议工具，不依赖FreeRTOS、Media、USB、文件系统或BSP。提供Validate、MatchesReply、MatchesCurrent、MakeReply、AllocateIds、Stage/Flush/AcceptReply/Clear/CancelReady、ReplayBegin/Complete/GetReply/Retire。

app_messages.c/.h增加独立LifecycleMessagesInit/Send/Receive。Init通过原Framework Application等级登记，不创建Task、不发送业务命令。

未来Task负责运输、App模块负责消费者事务、Camera负责资格与硬件，原Media账本负责帧引用。本阶段没有接入这条业务链。

## 构建与后续

Keil只在CM7 Target登记新C文件，CM4 Target及全部原TargetOption保持一致。C1不新增业务测试入口或Watch控制命令。

C2前必须审核消费者表、每消费者待发槽数量及安全退休规则、资格释放实际证明、管理员Shutdown关联、业务超时及迟到ACK处理。本协议工具和419项专项不代替这些未来业务验收。
