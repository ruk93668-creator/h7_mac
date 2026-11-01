> 主线集成说明：本模块已获用户批准按软件验收结果纳入main。下文“不提交、不推送、不合并”等语句是隔离验收期间的历史约束，不代表当前授权。实际合并和远端提交号见Git历史及操作交付报告。软件测试不等于实板验收。

# UVC-06 最终整改验收

本轮基线是UVC-06未提交隔离工作树，不是重新从main覆盖。工作区：`C:\Users\29062\.codex\worktrees\uvc-06\h7_mac`。HEAD仍为8abc24530a7b7d55953eed797f11ee16134ea719。本轮不提交、不推送、不合并。

## 根因及修复前复现

### A. 设备级GET_DESCRIPTOR中断Probe GET_CUR

1. 有效Probe SET完成，启动完整34字节Probe GET_CUR。
2. 不发送旧IN完成通知，直接发送设备级GET_DESCRIPTOR(Device,18)。
3. 真实Platform关闭/重新打开旧EP0 IN，ST Core自行处理新设备请求；UVC_Setup没有被调用。
4. 描述符IN完成时，真实Core仍调用UVC_EP0_TxSent。
5. 修改前只检查probe_reply_pending，错误设置probe_observed=true。新增用例0实际值1，期望0，失败。

这是Class授权标志跨请求残留。仅在UVC_Setup清理标志无法覆盖由Core直接处理的设备请求。

修复：TxSent消费旧pending标志前，必须同时匹配当前bmRequest=A1、bRequest=81、wValue=0100、wIndex=0001、wLength>=34，设备已Configured、EP0处于DATA_IN、实际总长度34且Probe有效。匹配才授权；不匹配仅清除pending，不改Probe、Commit及已有成功协商值。

设备请求的SETUP本身不通知Class，本方案不声称立即收到全局取消通知；旧标志在下一次Class Setup或完成回调中失效。它不能再凭其他事务的完成事件授权Commit。因此无需增加Core/Platform通用事务取消API。

### B. 迟到OUT完成使用旧SET_CUR接收区

1. 发出Probe或Commit SET_CUR，control_rx准备好34字节，pending_selector保留。
2. 新的设备级GET_DESCRIPTOR（或设备OUT SET_DESCRIPTOR）打断旧事务。
3. 注入迟到的UVC_EP0_RxReady。
4. 修改前只检查pending_selector和HAL接收计数，继续解码旧control_rx并返回USBD_OK；新增1/2/3及31–38证明缺少当前事务校验。

修复：一次性消费pending_selector，然后核对当前bmRequest=21、bRequest=01、完整wValue等于selector<<8、wIndex=1、wLength=34，EP0必须DATA_OUT且总接收长度34。不匹配返回USBD_FAIL，仅取消旧接收授权；不修改Probe/Commit，不对当前新事务补STALL。匹配后的真实短包/非法参数仍走原有显式STALL保护。

测试区分了两层：真实HAL OUT回调在新DATA_IN事务中应由Core忽略（用例4）；额外直接注入迟到Class回调用于验证防御性检查（1/2/3及31–38）。设备SET_DESCRIPTOR在当前Core中不支持，本报告不把它当作成功接收设备数据的功能。

### C. 标准接口GET_STATUS缺失

修改前UVC_Setup没有GET_STATUS分支，VC/VS合法请求被STALL。新增6/7/8/21失败。

修复：仅接受bmRequest=81、bRequest=GET_STATUS、wValue=0、wIndex=0或1、wLength=2。清零已有36字节、4字节对齐的持久control_tx，发送有效两字节`00 00`，不发送padding。12项非法字段测试保留严格拒绝行为。

## 修改范围及差异

本轮唯一生产源码修改：`CM7/Service/usb_uvc_class.c`。

- UVC_Setup新增标准GET_STATUS分支。
- UVC_EP0_TxSent新增当前请求、状态及总长度校验。
- UVC_EP0_RxReady新增当前请求与pending selector匹配校验；消费失效授权，不污染新事务。
- 增加内部UVC_IsControlRequest及提前声明、简短中文注释。

`CM7/Platform/usb_device.c`与本轮开始时逐字节相同。已通过的stalled检查、EP0_STALL恢复、busy/PIO所有权保护、新SETUP恢复路径均未修改。

描述符、Camera、Media、BSP、HAL、官方ST Core及CM4源码均与本轮开始时一致。审计见final_audit.json。

测试修改：

- 新增cancellation_test.c和run_cancellation_tests.py，39项均为新增测试，没有替代原有用例。
- 原probe_test.c只补齐直接调用Setup的夹具：先保存s_device.request，再调用UVC_Setup。真实Core本来就这样保存当前请求，完成回调现在依赖该真实现场。原89个编号、测试主体和断言保留，没有放宽断言。
- 既有套件的命令/结果JSON由实际重跑生成；生产代码未在测试中修改。
- 最终整改时已交付final_review.patch并通过反向检查；它是一次性审核证据，原始副本归档在仓库外的uvc06_main_merge_backup，未作为长期测试工具加入main。完整UVC-06主线差异通过git diff 8abc245..main查看。

## 新增测试修复前后

修改前：39项，15 PASS、24 FAIL。修改后：39项，39 PASS、0 FAIL。

失败分类：Tx授权问题9项，Rx事务匹配问题11项，合法接口GET_STATUS缺失4项。其余15项修改前即通过，修复后继续通过。

| 新编号 | 场景 | 修改前 | 修改后 |
|---:|---|---|---|
| 0 | Probe GET_CUR被设备描述符打断不得授权Commit | FAIL | PASS |
| 1 | 旧Probe SET_CUR回调不能处理设备GET_DESCRIPTOR | FAIL | PASS |
| 2 | 旧Commit SET_CUR不能由其他完成授权 | FAIL | PASS |
| 3 | 旧SET_CUR被设备OUT SETUP取消 | FAIL | PASS |
| 4 | 旧OUT中断不污染描述符IN | PASS | PASS |
| 5 | 正常Probe GET_CUR到Commit | PASS | PASS |
| 6 | VC GET_STATUS两字节 | FAIL | PASS |
| 7 | VS GET_STATUS两字节 | FAIL | PASS |
| 8 | 持久GET_STATUS响应与重复请求 | FAIL | PASS |
| 9 | GET_STATUS非法字段0 | PASS | PASS |
| 10 | GET_STATUS非法字段1 | PASS | PASS |
| 11 | GET_STATUS非法字段2 | PASS | PASS |
| 12 | GET_STATUS非法字段3 | PASS | PASS |
| 13 | GET_STATUS非法字段4 | PASS | PASS |
| 14 | GET_STATUS非法字段5 | PASS | PASS |
| 15 | GET_STATUS非法字段6 | PASS | PASS |
| 16 | GET_STATUS非法字段7 | PASS | PASS |
| 17 | GET_STATUS非法字段8 | PASS | PASS |
| 18 | GET_STATUS非法字段9 | PASS | PASS |
| 19 | GET_STATUS非法字段10 | PASS | PASS |
| 20 | GET_STATUS非法字段11 | PASS | PASS |
| 21 | STALL后接口GET_STATUS恢复 | FAIL | PASS |
| 22 | STALL后设备GET_DESCRIPTOR恢复 | PASS | PASS |
| 23 | TxSent当前事务字段校验0 | FAIL | PASS |
| 24 | TxSent当前事务字段校验1 | FAIL | PASS |
| 25 | TxSent当前事务字段校验2 | FAIL | PASS |
| 26 | TxSent当前事务字段校验3 | FAIL | PASS |
| 27 | TxSent当前事务字段校验4 | FAIL | PASS |
| 28 | TxSent当前事务字段校验5 | FAIL | PASS |
| 29 | TxSent当前事务字段校验6 | FAIL | PASS |
| 30 | TxSent当前事务字段校验7 | FAIL | PASS |
| 31 | RxReady当前事务字段校验0 | FAIL | PASS |
| 32 | RxReady当前事务字段校验1 | FAIL | PASS |
| 33 | RxReady当前事务字段校验2 | FAIL | PASS |
| 34 | RxReady当前事务字段校验3 | FAIL | PASS |
| 35 | RxReady当前事务字段校验4 | FAIL | PASS |
| 36 | RxReady当前事务字段校验5 | FAIL | PASS |
| 37 | RxReady当前事务字段校验6 | FAIL | PASS |
| 38 | RxReady当前事务字段校验7 | FAIL | PASS |

原始修复前结果：cancellation_before.json；修复后结果：cancellation_results.json。每项保留断言数、失败行、实际值、期望值和模拟器异常。用例23–38分别独立破坏请求字段/阶段/长度，避免复合判断少检查某一项。

## 全量回归

| 套件 | 原有项 | 新增项 | PASS | FAIL |
|---|---:|---:|---:|---:|
| Probe/Commit原专项 | 89 | 0 | 89 | 0 |
| USB Core | 204 | 0 | 204 | 0 |
| EP0 IRQ | 4 | 0 | 4 | 0 |
| UVC Lifecycle | 40 | 0 | 40 | 0 |
| Descriptor及字符串 | 40 | 0 | 40 | 0 |
| 本轮取消/GET_STATUS | 0 | 39 | 39 | 0 |
| 总计 | 377 | 39 | 416 | 0 |

测试命令（隔离工程根目录；Python依赖沿用本机原有Unicorn/pyelftools）：
```
python Tests/UvcProbeCommit/run_tests.py
python Tests/UvcProbeCommit/run_cancellation_tests.py
python Tests/UsbCoreDecoupling/run_tests.py
python Tests/Ep0IrqRace/run_race.py
python Tests/UvcLifecycle/run_tests.py
python Tests/UsbUvcDescriptors/run_tests.py
```

ARMCC5 V5.06 update7 build960编译真实Class、Platform、BSP、Core，Unicorn执行ARM机器码，HAL硬件仅沿用原有Mock。没有修改Mock让失败变成功。

## Platform保护的额外回归

- 原非法Commit和短OUT仍显式STALL。
- ST Core随后尝试USBD_CtlSendStatus，由已有Platform stalled检查拒绝，不进入HAL，不占用IN busy或PIO；EP0状态保持STALL。
- 新39项中的21/22分别验证STALL后合法接口GET_STATUS和设备GET_DESCRIPTOR恢复；原89项中的82也继续覆盖下一笔合法请求。
- 原204项及4项IRQ竞态回归保持通过；没有提前释放Abort失败时的缓冲区。

## 正式Target完整重编

CM7、CM4分别通过Keil `UV4.exe -r <project> -t h7_mac_CM7/CM4 -o <log>`完整重编，日志见cm7_keil.log、cm4_keil.log。最终数量由regression_results.json汇总。

## 尚未验证的边界

- 无实板USB连接，本轮不能宣称Windows UVC枚举、总线STALL波形、视频流或主机兼容性验证完成。
- 本轮没有修改先前Probe参数策略、1000Hz未来时间戳方案，也未实现PTS/SCR采样。
- 不新增EP1、Alt1、Payload等功能；USB Mode仍按原逻辑。
- 本次Class检查识别“当前请求不同”的旧完成；同样字节的新旧事务及真实硬件迟到IRQ仍依赖已通过的Platform/BSP取消与清中断机制，本轮没有新增事务代号或修改该机制。
- 正式main及原工作区没有应用本轮修改，保留原有用户草稿。等待人工审核，不提交、不推送、不合并。
