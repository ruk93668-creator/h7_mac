# UI2.1 页面模型与接口设计

本文是 UI2.1 的首个审核件。接口及页面表为实施契约，当前提交只包含设计。默认正式镜像继续显示 UI1 四行 IPC 监控。

## 基线与可修改范围

- 分支 `codex/cm4-oled-ui`，工作树 `D:\32\STM32H7\h7_mac_oled_ui`。UI1 生产提交 `085c22f`，验收及 UI2 起点 `2e77ef9341d1c64485ec082494d98439821aebb8`。执行 `git status`、分支跟踪检查、`git remote -v`、`git fetch origin`、`git worktree list` 与 `git log -5 --oneline` 后，本地 HEAD 与跟踪引用一致。
- 本地既有改动为 `MDK-ARM/h7_mac.uvoptx` 与 `MDK-ARM/h7_mac.uvprojx`。本设计提交不暂存二者。实施阶段如需登记新文件，应基于 HEAD 生成仅含 CM4 Target 两个新源文件的改动，独立检查索引差异，并单独核对工作树中的本地设置。构建基线及测试版使用独立快照。
- 计划新增 `CM4/Application/oled_ui_page.c/.h`、`oled_ui_menu.c/.h`，在 `app_oled.c` 增加编译期开关及接入点。测试放入 `Tests/OledMonitor/`，说明放入 `Docs/UI2/`。保持 Device、BSP、Framework Timer、IPC 协议与 CM7。
- 参考 [OLED_UI 的 MenuPage / MenuItem](https://github.com/bdth-7777777/OLED_UI#3-创建自己的第一个列表类菜单) 的页面与条目分离思路。UI2 使用编号、固定数量和只读表，不引入参考项目的运行时链表、回调业务绑定或刷新实现。

## 模块职责

| 模块 | 责任和依赖 |
|---|---|
| `oled_ui_page` | 静态页面树、运行状态、事件处理、查询与校验。依赖通用结果类型与定宽整数，不包含 OLED、IPC、Framework 头文件 |
| `oled_ui_menu` | 根据只读状态和描述符排版。只使用 UI1 有界文字、区域反色、可写校验和 Device clear；不提交刷新 |
| `oled_ui` / `oled_ui_draw` | 保留已验收的 IPC 四行监控和 UI1 绘图接口 |
| `app_oled` | 初始化可选菜单控制器、消费测试事件、在原刷新事务中选择 Renderer，持有已有 512 字节备份 |

审计确认 `App_Init` 调用 `App_OledInit`，CM4 主循环先运行 Framework 事件和 Timer，再调用 `App_Run` 推进 OLED。现有 Timer 每 100 ms 回调，非 IDLE 提前返回；进入 IDLE 后才取得 IPC Service 本地快照、备份、渲染、提交。Device 在 `WAIT_CMD` 和 `WAIT_DATA` 中逐页读取同一帧缓冲。UI2 不改变这些读写边界。

## 固定页面表

`s_pages[OLED_UI_PAGE_COUNT]` 与各菜单项数组都为 `static const`。页面 ID 连续并显式分配。内部查表先判 ID 取值范围，才转为数组索引。描述符含 ID、父 ID、页面类型、标题、静态说明或只读菜单项数组和明确计数。菜单项只含静态标签、动作类型与目标 ID。禁止将业务地址作为菜单项数据。

| ID | 页面 | 父页面 | 内容 |
|---:|---|---|---|
| 0 | MONITOR | 无 | 启动页面，原四行监控 |
| 1 | SYSTEM | MONITOR | SYSTEM MENU：Camera / Network / Storage / About / Back |
| 2 | CAMERA | SYSTEM | CAMERA：Status / Format / Back |
| 3 | NETWORK | SYSTEM | NETWORK：Back，覆盖单项菜单 |
| 4 | STORAGE | SYSTEM | STORAGE：Info / Back |
| 5 | CAMERA_STATUS | CAMERA | 静态说明，无菜单项 |
| 6 | CAMERA_FORMAT | CAMERA | 静态说明，无菜单项 |
| 7 | STORAGE_INFO | STORAGE | 静态说明，无菜单项 |
| 8 | ABOUT | SYSTEM | 静态说明，无菜单项 |

上限为 **9 页、每页 5 个菜单项、3 个可见项、距根最多 3 次跳转**。叶页与监控页无选中项。只允许向直属子页进入，父关系固定，每次 Back 只向上一个层级。编译期检查表长与数组容量，软件测试遍历所有描述符验证父链、条目目标、重复 ID、计数和空指针一致性。

运行态由调用者拥有，包含 `uint32_t revision`、当前页 `uint8_t current` 和 9 份 `uint8_t selected, first_visible`。按 ARM ABI 预计 **24 B**，实施时编译期限制为 **≤24 B**。每页保留游标和窗口到下一次 Init。无堆、无递归、无返回栈。调试邮箱另预算 **16 B**（请求序号、事件、应答序号和结果各 32 位）。两项合计目标 **≤40 B** 新静态状态，实际 RAM/Flash 以 Keil map 为准；设计阶段未测。

## 接口与状态转换

`OledUi_PageInit` 初始化状态为 MONITOR，所有游标和窗口置 0。
`OledUi_PageOpen`、`OledUi_PageBack`、`OledUi_PageMoveNext`、`OledUi_PageMovePrevious` 是可单独测试的状态变更接口。
`OledUi_PageDispatch` 接收 `OPEN_MENU`、`NEXT`、`PREVIOUS`、`SELECT`、`BACK` 五种软件事件。
`OledUi_PageGetCurrent`、`OledUi_PageGetSelected` 返回当前只读描述符或条目，经输出参数传出。无选中项返回 `SYS_ERR_NOT_FOUND`。失败不改变输出参数。接口名采用 CM4 现有前缀和 `SysResult_t`。

| 输入 | 条件 | 行为 |
|---|---|---|
| Init | 有效状态指针 | 回 MONITOR，从初始游标开始 |
| Open | 正当直属子页 | 按保存的选中项和窗口显示目标页 |
| Open | 同页 | 成功，无变化 |
| Open | ID 越界 / 合法但非直属子页 | 分别返回 INVALID_ARG / INVALID_STATE，原状态不变 |
| Back | 已在根页 / 非根页 | 成功原地停留 / 回直属父页并恢复游标及窗口 |
| Next、Previous | 非空菜单 | 首尾钳位，不循环。选中项越过可见窗口时移动窗口，使其保持可见 |
| Next、Previous、Select | 无菜单项 | 成功，无变化 |
| Select | 选中项指向子页或 Back | 按条目动作进入子页或返回 |
| Open Menu | MONITOR / 菜单侧 | 打开 SYSTEM / 成功，无变化 |
| 任一接口 | NULL 或未知事件 | INVALID_ARG |
| 状态变更 | 已初始化但内部游标、窗口或当前页无效 | INVALID_STATE，原状态不变 |

已初始化状态的不变式为 `current < PAGE_COUNT`。非空菜单满足 `selected < count`、`first_visible <= max(count-3,0)`、`first_visible <= selected < first_visible+3`。无选中项的页面中两个游标字段均为 0。接口先验证索引、关系和目标状态，再提交变更。失败与边界无动作均不改变 `revision`，实际改变才更新版本，允许无符号回绕，不能用大小比较判断新旧。每次调用顺序处理一个事件，连续输入按其调用顺序生效。状态仅由 CM4 主循环驱动，不在 IRQ 中读写共享控制器或绘图。

## 菜单与刷新接入

- 128×32 上以 8 像素为一行，标题在 y=0，菜单行在 y=8、16、24。标题 x=3 最多 20 个字格，行标记 x=3，条目 x=15 最多 18 个字格。先画正常文字和选中行的 `>`，再反色该行。长菜单只迭代可见的至多三条。静态叶页使用相同标题和有界说明。
- 菜单 Renderer 先校验整份逻辑输入及 Device 可写状态，再调用 clear。任何 Ops 失败立即透传。由 `App_OledRefresh` 对渲染或刷新首提交失败使用已有备份进行回滚。事件在刷新中允许更新下一帧的逻辑状态，当前传输的 Framebuffer 保持原样。逻辑状态不随显示提交失败回滚，后续刷新会重试最新状态。
- 正式镜像的 `OLED_UI2_TEST_INPUT` 默认关闭，无菜单入口和测试邮箱。临时调试构建打开后也以 MONITOR 启动。调试器先写事件，再更新请求序号，等待应答序号后才写下一条。主循环在现有 App 入口消费，单槽只允许一个未应答请求。自动软件测试直接向 Dispatch 投递连续事件，验证其确定性而不依赖调试器吞吐。菜单仅在原 Timer 的 IDLE 事务内显示，继续通过 IPC Service 获取原快照，不改变现有提交节奏和错误行为。
- Keil 工程使用 ARMCC 5.06u7 b960、`h7_mac_CM4` 和原 CM4 Scatter，新增源只登记到 CM4。测试构建须核对 map 中实际链接的控制器和菜单 Renderer；默认构建须确认未链接测试入口。

## 验证与交付计划

| 阶段 | 可复核结果 |
|---|---|
| UI2.1 页面模型 | 本设计先审，后续提交常量表、ID 校验、容量断言及模型测试 |
| UI2.2 状态机 | 测试空页、单项、首页返回、子页恢复、连续事件，以及失败前后状态逐字段相同 |
| UI2.3 菜单 Renderer | 比较四行区域、高亮、滚动和文字边界的像素，覆盖长菜单 |
| UI2.4 App 集成 | 验证忙期间收到事件不改旧帧，回到 IDLE 后按新状态生成；注入渲染与刷新提交失败，比较原帧 512 字节并验证重试 |
| UI2.5 回归与交付 | O0/O2 两组运行原 69 个 OLED 用例及新增测试；分别完整构建基线、正式 UI2、菜单测试版，记录 Keil RAM/Flash 和链接结果；准备临时实板工程及脚本 |

UI2.1 设计提交没有可执行变更。测试、RAM/Flash 增量及实板均为 NOT_RUN。实板验收准备与后续阶段完成后仍需审核，UI3 与 main 合并不在本阶段范围。
