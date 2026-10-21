# Framework 架构说明

Framework 是一个双核分别运行、静态资源管理、主循环驱动的协作式框架。

## 初始化与运行链路

```text
main / Bootstrap
    │
    ├─ HAL、时钟、双核启动、IPC EarlyInit
    │
    ├─ Framework_InitRuntime()
    │    PLATFORM → BUS → DEVICE → MEDIA → SERVICE → APPLICATION
    │
    └─ while (1)
         └─ Main_Run()
              └─ Framework_Run()
                   ├─ Framework_EventDispatch()
                   └─ Framework_TimerProcess()
```


## 模块职责

| 模块 | 职责与当前实现 |
|---|---|
| Initcall | 模块通过宏注册到 `.framework_init`，按等级初始化；首次失败立即返回，停止当前等级及后续等级的初始化。 |
| Runtime | 提供统一运行入口，每轮先处理事件，再处理 Timer。 |
| Event | 发布/订阅机制；16个订阅、16个 FIFO 槽位，每轮最多分发4个事件。一个事件可通知多个匹配订阅者。 |
| Timer | 16槽对象池＋最小堆；支持单次、周期模式；通过索引＋Generation Handle 管理生命周期，每轮最多处理一个到期对象。 |
| List | 侵入式双向循环链表；提供初始化、判空、尾插、删除和 `FRAMEWORK_CONTAINER_OF`。 |
| Result | `SysResult_t` 表示单次操作结果：0成功、负数失败。 |
| IPC Fault | 独立位标志记录累计故障，与操作返回值分开。 |

## 双核与分层关系

- **Common**：共享接口、类型、协议和基础工具；公共头文件不代表两核运行状态共用。
- **CM7 / CM4 Framework**：各自保存事件队列和 Timer 对象池；Timer Handle 仅在所属核心有效。
- **Application**：实现业务流程，通过框架接口组织事件和定时任务。
- **Service**：实现协议和服务，例如 IPC 初始化。
- **Device**：描述设备及生命周期接口；当前 CM7 注册已使用 List 实现参数、重名和状态检查。
- **Platform**：封装时间、内存屏障等底层能力。
