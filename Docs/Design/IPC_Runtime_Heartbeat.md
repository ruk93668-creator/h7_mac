# IPC运行期Heartbeat：接口与实板验收

完整修改、权限和软件证据见 [05C-1报告](../../Tests/IpcRuntime/verification_report.md)。本轮运行期实板结果尚未填写。

## 对外接口

CM7 Application在调度器启动前调用IPC_Service_SetRuntimeOwner登记身份；只有IPC Task在BOOT后调用IPC_Service_RuntimeStart与RuntimeProcess。普通Application业务不能调用发布接口替代IPC Task。CM4只使用Service私有Framework Timer。

两核普通线程均可调用：

```c
IpcRuntimeStatus_t status;
SysResult_t result = IPC_Service_GetRuntimeStatus(&status);
if (result == SYS_OK)
{
    //status.peer_state表示对端观察状态，不以Ready或Task.result代替
}
```

输出必须在本核普通内存，不能指向4KiB共享窗口；ISR禁止调用。Core的ReadLocal/ReadPeer仍提供完整共享快照，运行期授权没有放宽这些检查。

## Keil Watch

两核使用各自ELF观察同名本地变量：

```c
g_ipc.header
g_ipc.m7.heartbeat
g_ipc.m4.heartbeat
g_ipc.m7.sequence
g_ipc.m4.sequence
g_ipc_runtime_debug
```

CM7额外观察：

```c
g_app_tasks_debug.created_tasks
g_app_tasks_debug.boot_released
g_ipc_task_debug
```

预期created_tasks=4、boot_released=1、g_ipc_task_debug.boot_seen=1。心跳标称每100ms增加1，sequence每次成功发布加2。调度推迟不补发历史心跳，因此不能用瞬时计数推算精确运行时间。

| peer_state | 数值 | 说明 |
|---|---:|---|
| STARTING | 0 | 首次快照或等待后续心跳 |
| ONLINE | 1 | 已观察到新值且未超时 |
| STARTUP_TIMEOUT | 2 | 运行期启动后500ms未取得进展 |
| TIMEOUT | 3 | 曾观察进展，随后500ms无进展 |
| PROTOCOL_ERROR | 4 | 本次协议不匹配 |

重点观察peer_age_ms、read_result、publish_result、busy_reads、protocol_errors、timeout_events、startup_timeouts、scheduling_delays、max_step_gap_ms、fault_flags，以及CM7 stack_free_words。

## 实板步骤

1. 同一分支编译并烧录两核正式Target，完整断电后启动；不要混用05B/05C或V1固件。
2. 不在HSEM/初始化过程中打断点。运行数秒后确认Header布局不变、两核心跳持续变化、最终双方peer_state=1。
若CM4已开始Timer，而CM7仍在较长的初始化或BOOT等待中，CM4可能先记录STARTUP_TIMEOUT；这与运行中失联分开计数。CM7开始发布并被观察到后续变化后才能ONLINE，历史启动超时位仍保留。

3. 确认原USB视频、Camera及OLED功能正常。视频高负载下观察max_step_gap_ms和任务最小剩余栈，记录实测值；不能直接把100ms配置当实测周期。
4. 如调试环境支持仅暂停一个核而另一核及其本地Tick继续运行，可暂停CM4超过500ms，观察CM7 TIMEOUT；恢复CM4后应重新观察到变化并ONLINE。反向同样验证。该操作是暂停/继续，不是单核Reset。
5. 若调试器暂停所有核或Tick也停走，此方式不能验证超时，应记录未执行，不将结果算失败或通过。暂停本身也会影响原视频路径，测试后按现有安全流程恢复或配套断电重启。
6. 观察恢复后timeout_events历史保留、fault_flags仍保留Heartbeat超时位，但当前peer_state可回到ONLINE。这不是错误恢复失败。

Tick/心跳回绕、未知协议、BUSY及Timer故障已经在隔离软件中覆盖，不建议直接在实板Watch中改共享字段绕过所有权。单核热复位与新会话识别不属于本阶段。

## 验收记录（待用户实板反馈）

| 项目 | 状态 |
|---|---|
| 配套冷启动及双向ONLINE | 未验证 |
| 心跳实际周期及调度最大间隔 | 未验证 |
| 单核暂停导致对端超时、继续后恢复 | 未验证 |
| IPC Task最小剩余栈 | 未验证 |
| 原USB视频/Camera/OLED运行期回归 | 未验证 |
