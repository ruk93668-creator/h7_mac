# 生命周期队列初始化 BASEPRI 兼容性修复

日期：2026-11-14。基线：`2a927374c5cbdaa5e6bca8267e0ccf500d041045`。
独立分支：`fix/lifecycle-init-basepri`。不修改main，不推送R2分支，不修改PR #7既有提交。

## 1. 现场和根因

用户实板现场：`App_LifecycleMessagesInit()`首次执行，BASEPRI=0x50、s_lifecycle_attempted=0，返回SYS_ERR_INVALID_STATE(-4)。原入口条件强制BASEPRI=0，与FreeRTOS调度器启动前的正常内核准备状态不兼容。

实际顺序没有改变：

```text
CM7 main
  App_TasksCreate
    App_EventsInit
    App_MessagesInit（旧消息队列）
    Camera/USB/Control/IPC/Storage 静态任务创建
  Framework_InitRuntime
    PLATFORM → BUS → DEVICE → MEDIA → SERVICE → APPLICATION
    App_LifecycleMessagesInit（独立生命周期静态队列）
  App_TasksRecordInitialization
  Platform_RtosStart
```

FreeRTOS的静态队列创建经过prvInitialiseNewQueue→xQueueGenericReset→taskENTER_CRITICAL；当前ARM_CM7/r0p1 Port的uxCriticalNesting初值为0xaaaaaaaa，vPortExitCritical只有嵌套计数降到0才重新使能中断。调度器启动前已有内核对象创建，因此BASEPRI非零不能独自证明该初始化上下文非法。

源码依据：main.c的App_TasksCreate在Framework_InitRuntime之前；queue.c:365、543、571、309；port.c:150、431、448；app_messages.c:169。

## 2. 唯一生产修改与完整Diff

只修改 `CM7/Application/app_messages.c` 中App_LifecycleMessagesInit的检查，新增两行中文原因注释；实际逻辑Diff为：

```diff
 if (s_lifecycle_attempted || Platform_SyncGetException() != 0U ||
-    Platform_SyncGetPrimask() != 0U || Platform_SyncGetBasepri() != 0U ||
+    Platform_SyncGetPrimask() != 0U ||
     xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
     return SYS_ERR_INVALID_STATE;
```

首次/重复初始化、非ISR、PRIMASK=0、调度器NOT_STARTED检查全部保留。仅允许启动前BASEPRI非零，不要求它必须等于0x50。

没有清零BASEPRI，没有修改FreeRTOS Port/临界区、初始化顺序、队列容量、56B/52B消息、任务优先级、Replay或R2停止路径。运行期Lifecycle_Context、Send、Receive逐字未改，非零BASEPRI仍拒绝收发。

`audit_scope.py`将生产文件与基线对照，验证函数外正文完全一致、函数内仅删除批准的条件。原C1/C1.1/C2.1.1测试源及脚本字节不变。构建工程文件不变。

## 3. 新增专项

`init_test.c`复用原C1测试依赖，编译真实app_messages.c、真实FreeRTOS queue.c/list.c；任务上下文和Port边界为测试桩，不模拟真实板卡。

| 场景 | O0 | O2 |
| --- | --- | --- |
| BASEPRI=0首次成功、重复拒绝 | PASS | PASS |
| BASEPRI=0x50首次成功、重复拒绝 | PASS | PASS |
| 其他非零BASEPRI=0x80首次成功 | PASS | PASS |
| PRIMASK非零拒绝，不消耗初始化机会 | PASS | PASS |
| ISR拒绝，不消耗初始化机会 | PASS | PASS |
| Scheduler RUNNING拒绝 | PASS | PASS |
| Scheduler SUSPENDED拒绝 | PASS | PASS |
| 第二次队列创建失败后保持失败锁存 | PASS | PASS |
| 运行期发送BASEPRI=0x50拒绝，队列/通知不变 | PASS | PASS |
| 运行期发送BASEPRI=1拒绝 | PASS | PASS |
| 运行期接收BASEPRI=0x50拒绝，队列/输出不变 | PASS | PASS |
| 运行期接收BASEPRI=1拒绝 | PASS | PASS |
| 恢复合法上下文后完整消息传输及56B/52B ABI | PASS | PASS |

合计26/26 PASS。测试不依赖放宽原断言，原用例没有删除或跳过。

注意：专项中的Port是边界桩，输入BASEPRI在桩中可保持不变；这只核对适配层没有主动清零/改写。真实FreeRTOS创建队列可能合法提升BASEPRI，不能据此承诺真实BASEPRI=0进入初始化后始终不变。本轮没有修改真实Port行为。

## 4. 原协议回归与ARMCC5

| 验证 | 实际结果 |
| --- | --- |
| C1原专项 | 419/419 PASS |
| C1.1新增专项 | 24/24 PASS，连同原C1共443 |
| C2.1.1原专项 | 648/648 PASS，保留40960次压力事务 |
| C2.1.1 ABI | 两种优化探针通过 |
| 原回归判定器 | 29/29 PASS |
| CM7正式全量编译 | 0 Error、0 Warning |
| CM4正式全量编译 | 0 Error、0 Warning |

新增专项使用实际ARMCC5，分别O0/O2构建并执行；两核使用既有Keil/ARMCC5正式工程完整重编。原协议回归在git archive隔离副本中执行，覆盖本次候选app_messages.c，运行前删除副本旧报告，避免把历史结果当新结果。

未运行本轮要求之外的全仓历史回归，不宣称全仓全绿。R2原报告保留的两项旧B1严格轨迹差异没有因此解决，也没有修改其断言。

## 5. 构建产物与日志

- `h7_mac_CM7_rebuild.log`、`h7_mac_CM4_rebuild.log`：两核原始日志。
- `build_results.json`：命令、退出码、错误/警告数、工程未改验证。
- `results.json`：26项逐用例结果。
- `protocol_results.json`：原协议回归的实际报告。
- `scope_results.json`：修改范围及受保护源文件核对。
- `firmware_manifest.json`：本次现有AXF路径和SHA256。

原正式输出在重编前备份至 `output/LifecycleInitBasepri/build/previous_output/`；没有清理既有output。Keil输出目录、名称和配置不改；CM7原CreateHexFile=0，生成AXF/MAP；CM4原配置生成AXF/HEX/MAP。构建产物不提交Git。

## 6. 实板复测步骤（尚未执行）

1. 使用本分支新生成的CM7 AXF及配套CM4 AXF，核对firmware_manifest。R2旧HardwareAcceptance文档中的固件哈希不适用于本次新CM7镜像，不能混用旧符号。
2. Download并Verify，完整断电启动。不要在Watch手动清BASEPRI、primask或s_lifecycle_attempted。
3. 在App_LifecycleMessagesInit入口观察首次attempted=0、IPSR=0、PRIMASK=0、Scheduler NOT_STARTED；允许BASEPRI为0x50或其他非零值。不要通过调试器调用RTOS函数来伪造现场。
4. 单步越过入口检查，确认进入三次xQueueCreateStatic，最终s_lifecycle_ready=true、返回0。记录创建失败时的真实结果；不能将它清零继续。
5. 确认Framework_InitRuntime继续完成，main的init_error不再停在-4，进入Platform_RtosStart及原BOOT路径。
6. 撤销断点恢复无暂停运行，验证Camera/USB/IPC/OLED原功能。核对初始化修复与后续R2停止验收分开记录。
7. 运行期非零BASEPRI拒绝收发已由软件专项覆盖；本轮不要求在实板任意修改CPU屏蔽寄存器试验。需要进一步硬件注入时另行设计，不能在临界区强发消息。

| 实烧CM7/CM4 | 入口BASEPRI/IPSR/PRIMASK | attempted | 三条队列/ready/返回值 | Framework完成 | RTOS/视频/IPC/OLED | 结论 |
| --- | --- | --- | --- | --- | --- | --- |
| 待填 | 待填 | 待填 | 待填 | 待填 | 待填 | 未执行 |

## 7. 交付边界

这是启动前检查的兼容性修复，不新增Watch控制邮箱、不接入消费者生命周期、不改变正常START/STOP。实板确认仍待用户执行。main和PR #7分支保持原提交，仅提交推送独立修复分支。
