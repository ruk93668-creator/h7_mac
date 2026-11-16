# Cross-task Consistency Contract

基线c3350b0。以下区分SOURCE_CONFIRMED、DESIGN_REQUIREMENT与NOT_PROVEN，不以设计要求冒充当前实现。

## 唯一写入所有者及读责任

| 状态 | 写owner | 合法读取及边界 |
|---|---|---|
| s_camera业务/Qualification/两个Rx | CameraTask | App_Camera及Qualifier在Camera上下文；不从Control/USB直接推进硬件 |
| s_camera_bridge | CameraTask | Camera Bridge函数；held/offer/return/ACK状态由该任务推进 |
| s_usb_bridge、App_USB消费者 | UsbTask | USB Bridge和App_USB；Camera不可跨任务裸读结构 |
| Storage业务/文件/接收策略 | StorageTask为业务owner | Camera通过既有Offer/ProcessReturns共享交接入口；字段不是全由同一任务写，需逐字段同步 |
| Storage slot/reserving/queue | Camera Offer与Storage Process、Camera Returns分阶段写 | 原PRIMASK保存/恢复、屏障，state/generation/session核验保护交接 |
| FIFO loans/usb_owned/sd_owned | CameraTask经Media公开获取/归还路径 | DMA/BSP回调还修改块完成/parser关联元数据；不能宣称整个s_fifo仅Task写，按原临界区及回调契约 |
| Debug/App_Status | 各原诊断发布点 | 只投影，不作为退出权威 |

Storage Offer在锁内预留slot并递增generation/offering，锁外Retain，再锁内降低offering并核对accepting/session决定QUEUED或RETURN_PENDING。STOP竞入不能丢已Retain lease。Process选slot为WRITING后锁外做文件IO，结束锁内转待归还；Camera Return保存凭据并真实释放后同generation才FREE。读多个字段必须在同一个原同步窗口，不能读一半后调业务再拼另一半。

PRIMASK只约束本CM7可屏蔽中断，不是跨双核通用锁；本路径没有证据支持CM4共享写同一账本。若发现CM4或别核参与，结论UNKNOWN，另审，不自行增加同步框架。内存屏障不代替所有者或消息顺序。

## 队列顺序与调度

同一静态队列按发送入队顺序FIFO；普通帧队列、业务命令队列和56B生命周期队列彼此独立，不存在跨队列全局FIFO。通知合并不能携带可靠资源状态。生命周期每轮Camera最多接收一条，原Return可在Bridge循环最多四条中处理；不能因RELEASE先被取出就认定所有TO_CAMERA帧消息已清空。

正常USB持帧路径的ACK消费→本地ModeStop→End/Flush顺序可以由同Task代码确认；Camera看到RELEASE则间接知道遵守这条程序顺序的消费者已经执行该步，但尚需身份关联及迟到交接检查，不能把一条RELEASE当自报FRAMES_DRAINED证明。

## 退出期间重新开放交接的路径检查

1. USB CanTransfer在stopping/started门控关闭后，BridgeUsbProcess(false)不会从IDLE发新请求；REQUEST未入队可撤销，已入队则发Cancel，LOANED等待安全取消/归还。SOURCE_CONFIRMED。
2. 当前App_UvcBridgeCameraProcess的FRAME_REQUEST处理只检查command_id非零、held/request_pending等，不核对Qualification grant/generation。Camera端enabled是整体App_CameraCanOfferFrame，不是每消费者授权门。旧请求与独立生命周期队列的组合次序是否都被USB既有状态机穷尽排除，需要逐路径证明；不能宣称RELEASING已实现Camera端专用冻结。NOT_PROVEN。
3. Storage Offer锁内检查s_accepting及slot可用；已在Retain的交接STOP后转RETURN_PENDING，不直接清slot。Storage_LifecycleFinish检查quiet和slot FREE，但它属于消费者侧条件，不等同Camera独立核验。SOURCE_CONFIRMED部分。
4. Storage新START由pending_start/ConsumerBegin/grant和本地文件条件约束，不应绕过旧grant；但Camera核验API尚未实现，退出期间是否存在从旧事务/排队命令重新accepting的完整不变量仍需证明。NOT_PROVEN。

## 有效范围及建议契约

DESIGN_REQUIREMENT：核验绑定grant G和当前受控RELEASE R。Qualification负责R窗口/Replay验证；资源层核验G对应的交接与loan。两者不是同request_id。关闭入口必须在G退出阶段单调保持；读真实资源状态到撤销资格之间不能执行可重入业务或让该G重新接收frame；不满足则BUSY/拒绝。out仅当次证据，不持久第二套引用数。

Camera不能直接跨任务读取USB state并与Storage/FIFO历史快照相加。建议先证明USB本地顺序并在Camera对应Bridge/Media权威字段核对；必要新增关联/关闭事实时必须单独审核字段owner、写点、清除条件与RAM。若现有状态足够则不新增，若不够则明确NOT_PROVEN，不凭空声称接口可立即实现。

一个消费者退出时，只撤销其真实owner位/资格；另一方持帧可保持，最后owner才归还块引用。最后资格退出还需Qual_OtherWork无活动/事务/候选等、真实idle、Media和Bridge安全条件，并由FinishRound推进停止。NULL released/idle保持阻塞，不能从所有entry INACTIVE就推出硬件已停。

本轮不定义新状态机、引用账本、消息类型或强制回收。证明关闭边界和完整身份关联是API批准前置条件；真实并发/硬件故障仍非本轮验证。
