# IPC V2 协议与接口

> 本文记录05B的历史V2设计。05C-2当前采用正式V3扩展，请见IPC_V3_ApplicationStatus.md；不能混烧V2和V3。

本阶段的完整实现、字段偏移、调用链、所有权、测试证据及实板步骤见 [阶段05B验收报告](../../Tests/IpcV2/verification_report.md)。

- Common：ipc_shared.h定义320B协议；ipc_status.h定义普通本地输入/输出；ipc_core.c实现一致性。
- Platform：各核提供编译器屏障、DMB、短PRIMASK临界区和线程检查，不依赖RTOS。
- Service：CM7负责ColdStart和本核初始发布，CM4验证协议后只写自己的块；初始化结束关闭写窗口。
- Application：本轮不接入任何业务状态；将来通过Service获取本地快照，不访问g_ipc字段。

```c
IpcStatusSnapshot_t snapshot;
SysResult_t result = IPC_Service_ReadPeer(&snapshot);
if (result == SYS_OK)
{
    //snapshot是本地完整副本，但不证明新鲜或对端在线
}
```

只在普通线程中调用，不在ISR中调用。snapshot必须是本核普通内存，不能放入SRAM4共享窗口。失败不修改snapshot；BUSY由调用方稍后重试，不能忙等阻塞其他工作。

`IPC_Service_GetDiagnostics()`读取本核故障位及最近结果，不访问未知共享布局。共享fault_flags仍是可累计位字段；本阶段Service只初始化为0，没有业务故障发布者。

4KiB UNINIT窗口及Non-cacheable属性保持不变。本轮不支持V1混用、热复位、周期Heartbeat或新运行期发布者。不能根据Ready字段宣称所有业务已启动成功。
