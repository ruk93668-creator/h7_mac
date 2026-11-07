# IPC V3：Camera/USB状态与实板验收

完整字段偏移、读写契约和软件证据见 [05C-2报告](../../Tests/IpcStatus/verification_report.md)。

## 配套烧录

本分支为codex/stage05-ipc-status，协议Version=3，feature_flags=3。必须编译并烧录本分支的h7_mac_CM7和h7_mac_CM4，完整断电后启动。320B布局、4KiB UNINIT、MPU及HSEM流程不改，但与V2不二进制兼容，不能只更新一个核。

保留当前本地已验证MJPEG配置。当前工作区是320×240 MJPEG、目标10FPS；此用户未提交配置不属于本轮提交，换工作区时请核对实际配置，不把目标FPS当实测FPS。

## Keil Watch

CM7：

```c
g_app_status_debug
g_ipc.m7.application
g_ipc_task_debug.snapshot_result
g_ipc_task_debug.stack_free_words
g_camera_task_debug
g_usb_task_debug
g_control_task_debug
g_ipc_runtime_debug
```

CM4：

```c
g_ipc_application_debug
g_ipc_runtime_debug
g_ipc.header
```

正常稳定运行后：Header.version=3，feature_flags=3；双方peer_state=1；snapshot_result=0；g_ipc_application_debug.valid_mask=7、fresh_mask=7。Task_count仍4，没有新增任务或改变启停流程。

Camera状态0停止/1运行/2停止中；USB状态1仅表示原USB任务启动成功，不等于Windows正在取流。Control RUNNING=3。IPC格式JPEG=4、YUY2=3、RGB565=2、Y8=1、UNKNOWN=0，与Camera原枚举有显式映射差异。

## CM4统一读取

```c
IpcApplicationView_t view;
SysResult_t result = IPC_Service_GetApplicationStatus(&view);
if (result == SYS_OK && (view.fresh_mask & IPC_APP_CAMERA) != 0U)
{
    //view.application是本地完整副本，采集FPS为camera_fps_milli除以1000
}
```

只在CM4普通主循环上下文调用。返回SYS_OK不等于数据新鲜；初始valid/fresh为0是正常状态。通信异常后可保留旧valid副本，但fresh会清零；read_result和peer_state说明通信原因。

## MJPEG并行验收步骤

1. 配套烧录并完整断电启动，不在初始化中打断点。保持正常供电和已排除干扰的调试连接。
2. Windows按原已通过方式打开MJPEG视频。确认真实画面连续显示，原OLED及双核功能正常。
3. 查看两核Heartbeat持续变化、peer_state=1。CM4的valid/fresh最终均为7，receive_age_ms通常在发布周期内变化；数据年龄超过500ms后不得继续fresh。
4. 对比同一snapshot_id的CM7 g_ipc.m7.application与CM4 g_ipc_application_debug.application。若编号不同，表示异步采样的不同批次，应等待或重新取样，不能直接判数据错误。
5. 同编号时Camera/USB帧数、丢帧、FPS、状态、宽高、格式和结果应一致；CM4 *_age_ms会加接收后经过时间，不要求与共享原值逐字相等。
6. CM7各任务原调试变量持续变化，g_app_status_debug是最近汇总；它们最多有采样周期级差异。只有在同一采样窗口/编号下比对，不能要求两个异步Watch窗口每一瞬间完全相等。
7. 对当前320×240 MJPEG工作配置，成功启动过Camera后宽高应320/240、format=4。换配置则以实际成功应用值为准。
8. 持续运行至少5分钟，记录真实Camera FPS、USB完成FPS、Windows收到FPS、丢帧增量、scheduling_delays/max_step_gap_ms、IPC Task最小剩余栈；不得根据video_config目标值填实测栏。
9. 关闭/重新打开主机视频，确认原流程正常，USB完成计数与实测窗口相符。Camera和USB任务仍正常推进时，即使帧计数不增，来源记录仍可fresh；fresh不表示有新图像。
10. 如调试器支持单独暂停一个核而另一个核和本地Tick继续运行，可暂停CM7超过500ms观察CM4 fresh=0和超时；继续后新编号及新心跳恢复。此为暂停/继续，不是单核Reset。若调试器同时暂停两核，记录该项未执行。

Watch读取本身不是协议原子读取；若暂停两核检查，先确认CM7块sequence为偶数且编号相同，冻结在奇数写入点的内存不能当完整样本。不要直接修改共享字段制造“通过”。

## 新鲜度解释

valid_mask表示来源曾提交结构合法数据；fresh_mask表示本地观察期限和通信状态满足要求。位0/1/2分别Camera/USB/Control。

来源年龄包括发送方采样时年龄、同核等待到运行期入口的延迟和接收后的本地时间。两核绝对Tick不比较。处理入口到最终接收的调度/总线延迟没有同步时钟可精确测量，因此这不是物理采样时刻的硬实时证明。

来源超过500ms后年龄饱和，直到所属任务再提交；同一业务编号过期后不能因重复接收或Tick回绕变新鲜。Snapshot编号和Heartbeat都不是会话证明。单核热复位仍未实现。

## 实板记录（当前全部待验收）

| 项目 | 基线05C-1 | 05C-2 | 结果 |
|---|---|---|---|
| Windows真实MJPEG视频 | 待记录 | 待记录 | 未验证 |
| Camera实测FPS | 待记录 | 待记录 | 未验证 |
| USB实际完成FPS | 待记录 | 待记录 | 未验证 |
| Windows实际收到FPS | 待记录 | 待记录 | 未验证 |
| 5分钟丢帧增量 | 待记录 | 待记录 | 未验证 |
| IPC最大处理间隔ms | 待记录 | 待记录 | 未验证 |
| IPC Task最小剩余栈Word | 待记录 | 待记录 | 未验证 |
| 同编号跨核业务字段一致 | 不适用 | 待记录 | 未验证 |
| 通信异常/陈旧清除fresh | 不适用 | 待记录 | 未验证 |

本轮不新增OLED界面，不改变Camera/Media/USB的帧所有权及停机策略。
