# Phase5 未完成检查点

输入2c38c6240b2468beed504151745ce6aff06ca474，研究分支保持不变。当前只是草稿检查点，NOT_ACCEPTED，不得登记旧Keil/BOOT或接入硬件。

新增usb_format（委托Camera/真实UVC纯验证器）、usb_consumer（实际C1/C2.1.1 Tx工具的QUERY/ACQUIRE/RELEASE/STOP_IF_IDLE客户端）、usb_frame（单交接元数据/Return/ACK消费/退休端口）C/H草稿。未创建Frame池、物理refs或Camera资格表。客户端缓存G不是Camera权威。Frame Port只由Camera实际操作MR，USB端不得直接Get/Release。

实际检查：三个C单元ARMCC5 O0/O2编译无诊断；consumer有限烟测16/16通过，真实Tx/协议/代次工具被链接执行，但Camera回复是可控合成回复，不是完整Qualification/资源退出证明。初始链接缺3个未使用Legacy任务依赖，补测试陷阱（触及即BKPT失败），原失败日志保留。格式单元只有编译证据，本轮尚未完成真实UVC验证器链接与执行；Frame单元尚未完成执行测试。

尚未实现USB Lifecycle主模块、USB/Supervisor双端Endpoint、ACK/FAULT交付、Native顺序调度、STOP首次IO后后台释放协调和完整异常恢复。尚未执行Phase1～4/协议完整回归、负对照、CM7/CM4 Rebuild、ROM/RAM/Stack验收。本检查点不能称Phase5软件基础完成。

已识别待解决的接口边界：通用EMEndpoint.read的BUSY表示等待，不能直接用它传递已入队START的终结BUSY拒绝；也不能伪造稳定Lifecycle事实触发错误补偿。USB Endpoint必须明确拒绝/完成表达，保持B2 ID23有界BUSY和ID31成功重启后复位条件；不得为了测试通过放宽Phase3语义。真实R域关联、Backend/DMA/ACK证明继续受Phase4门禁约束。

两个资源NOT_PROVEN保留，所有真实集成/硬件均NOT_RUN。旧Application/UVC/Core/Bridge/Media/SDMMC/FatFS/Replay、52B/56B ABI、Phase1～4、Keil外部修改、M4延时、main和其他工作区未改动。仅此未完成草稿允许备份，后续工作仍属于原授权Phase5范围，不进入Phase6。
