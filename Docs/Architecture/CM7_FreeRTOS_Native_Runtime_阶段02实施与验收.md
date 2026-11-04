# CM7 Native Runtime 阶段02：Camera + USB UVC

日期：2026-11-04。基线：`4d463ba5506263358331182bd07f3e6c09902fb9`。

工作分支：`codex/cm7-native-runtime-stage02`。本轮不合并main。

**完成软件接入、97项新增专项和三种正式构建；尚未完成实板视频验收。历史测试并非全绿：386项失败与基线完全一致，另有134项旧夹具因架构调整未能执行，原Legacy专项11项仍未执行。**

## 1. 启动与任务职责

```text
main
  App_TasksCreate：事件后端 + 5个静态队列 + 3个静态任务
  Framework_InitRuntime：只准备硬件、设备、视频配置及Class注册
  App_TasksRecordInitialization
  Platform_RtosStart
    ControlTask发布BOOT_READY
      UVC：START Camera → ACK → START USB → ACK
      TEST：START USB → ACK
```

Initcall继续在Scheduler之前运行。没有在Initcall调用Camera Stream Start或USB Start；既有Device自动初始化等准备仍保留。TEST不启动相机数据面，但没有取消既有全局硬件准备，不能据此声称相机缺失时整个Initcall一定成功。

|执行者|唯一任务侧职责|
|---|---|
|CameraTask，优先级3|OV5640配置及Device Start/Stop；Media Process/Get/Release；采集错误处理；Camera侧租借账本|
|UsbTask，优先级4|USB Mode/Device/UVC任务侧操作；构包与确认；USB侧租借账本|
|ControlTask，优先级2|BOOT、START/STOP命令、ACK和超时；不直接调用Device、Media或USB数据面|
|DCMI/DMA ISR，优先级5|原TC/FRAME配对、CT/NDTR窗口检查、地址替换和三缓冲轮换；随后发普通C提示|
|USB ISR，优先级6|保留同步EP0、Class回调和Token记录；状态更新后唤醒UsbTask|

BSP、Device、Media、Service和Common没有引入FreeRTOS任务/队列类型。RTOS任务通知和静态队列只存在于Application及既有Platform RTOS环境。HAL、官方USB Core、FreeRTOS Port、CM4均未修改。

## 2. 事件与消息

`RuntimeEvent_*`仍是可以合并的唤醒提示，不作为消息交付或资源释放凭据。

- Media轮换提交后通知CameraTask；BSP锁存采集异常后通知CameraTask。
- SOF、DataIn、DataOut、Incomplete、SETUP/Alt、Reset、Suspend/Resume、Connect/Disconnect等通过普通C回调唤醒UsbTask。
- SOF回调不构包、不发送。UsbTask调用原`USB_UVC_Process()`，原SOF/Token规则仍限制每个SOF最多一次提交。
- Camera/USB的20ms等待超时用于枚举、停止恢复及丢失提示后的兜底，**不是ISO发送的20ms调度周期**。
- Control以通知唤醒，100ms兜底检查命令超时；命令超过5000ms未完成只报错，不假定停止或强制释放。

五个队列均为静态资源，每个4槽：Camera命令、USB命令、Control回复/请求、USB→Camera帧消息、Camera→USB帧消息。正式编译下每条消息36B，存储共720B，五个队列控制块共360B。队列满返回`SYS_ERR_NO_RESOURCE`，发送者保留待交付状态，接收者腾出槽位后通知发送者重试。

`App_MessagesSend()`成功只代表入队，通知失败不能把已经入队的消息误报为未提交。普通事件位、队列消息和操作ACK三个层次分开。

## 3. 帧租借协议

```text
UsbTask确认can_submit
  FrameRequest(request_id)
         ↓
CameraTask调用Media Get，取得IN_USE帧
  FrameOffer(request_id, stream_epoch, lease_id, frame)
         ↓
UsbTask调用SubmitFrame成功，进入LOANED
  等待frame_owned=false，或合法任务上下文下Class已安全销毁
         ↓
  FrameReturn(同一组凭据)
         ↓
CameraTask核对凭据，调用Media Release成功
  ReturnAck(同一组凭据)
         ↓
UsbTask清除本轮账本，才允许请求下一帧
```

- 队列只复制帧地址、尺寸、格式及凭据，不复制38400B整帧。原USB Payload/PIO仍按包拷贝，这是既有发送机制。
- Camera在发送Offer前就登记持有。Offer队列满不再次Get。
- Submit成功只是USB借用；Submit失败没有借用新帧，也必须经过Return/ACK归还Camera。
- Return队列满保留原凭据。ACK队列满保留成功结果，不能再次Release。
- Camera核对`stream_epoch + lease_id + request_id`；重复Return只重发缓存ACK，旧代次不能释放新帧。
- Request、epoch、lease到达`UINT32_MAX`后拒绝继续生成，不回绕复用身份。
- Reset、Disconnect或Alt0不等于可以立即释放Media。仍须由UsbTask确认UVC已经归还，再走Return/ACK。
- `completed_frames`、超时和错误码变化都不构成释放依据。

两个账本分别由自己的任务修改，不跨任务读取另一账本。配置在调度前准备后只读。Camera/USB调试拒绝及重试计数也分别维护，避免两个任务竞争加一。

## 4. 停止和故障

正常停止顺序：

```text
Control → STOP USB
  UsbTask禁止新提交，推进Cancel/Abort与Return/ACK
  租借清空后USB_Mode_Stop
  USB停止成功ACK
Control → STOP Camera
  CameraTask确认Media停止及引用已释放，再Device_Stop
  Camera停止成功ACK
Control → IDLE
```

采集异常继续保留原Media行为：危险DCMI/DMA先停止；仍有IN_USE时进入DRAINING，不能因硬件已停就回收USB使用的帧。

Abort、Close、BSP Stop、Media Release失败都保留真实状态，不伪造归还。持久失败可能使协调停止超时并进入ERROR；需要问题解除后显式再次请求Stop，完全停止后才允许Start。没有自动重启Stream或强制重置对象池。

任务保留首个故障原因，后续停止错误通过对应ACK报告。Control的`result`表示当前协调操作结果，`last_fault`保留原始故障，停止成功不等于之前采集成功。

正式Device Model在Start失败后会进入ERROR，现有Device_Stop不会接受ERROR状态。本轮不越权修改Device状态或扩展生命周期恢复API；这种情况保留ERROR和停止失败，不能宣称已恢复。新增集成测试使用正式Device Model验证该路径。

## 5. 配置、内存和统计

唯一默认业务配置仍位于`CM7/Application/video_config.c`：160×120、YUV422/YUY2、输出目标25 FPS。UVC和Media接收同一份参数，不反向读取对方上下文。

- 默认UVC；编译CM7时定义`APP_VIDEO_TEST_MODE=1`选择TEST。
- 只支持冷启动选择，不新增热切换。
- UVC端点使用基线现有FS EP1 IN、1023B最大包；未改描述符和Probe/Commit。每包扣除2B Header，38400B需要38包，末包625B。

|资源|范围/大小|
|---|---|
|Camera任务栈|6144B，8字节对齐|
|USB任务栈|8192B，8字节对齐|
|Control任务栈|3072B，8字节对齐|
|Idle任务栈|1024B|
|静态队列存储/控制块|720B / 360B|
|SDRAM保留区|0xD0000000～0xD00003FF|
|三块40KiB缓冲|0xD0000400、0xD000A400、0xD0014400|
|三缓冲结束，不包含|0xD001E400|
|既有MPU业务窗口|0xD0000000～0xD001FFFF，128KiB|
|IPC共享区|g_ipc仍在0x38000000，32B，布局未改|

Map/AXF审计验证静态资源无重叠、向量指向原SVC/PendSV/SysTick实现，没有Legacy轮询入口、动态malloc或测试图像链接进正式任务路径。实际任务栈压力余量仍需实板观察。

统计变量：

|Watch变量|含义|
|---|---|
|g_control_task_debug.state|0空闲、1启动Camera、2启动USB、3启动已确认、4停USB、5停Camera、6错误|
|g_control_task_debug.result / last_fault|当前协调结果 / 原始故障|
|g_camera_task_debug.captured_frames / dropped_frames|Media完整帧与丢帧计数|
|g_camera_task_debug.measured_fps_milli|设备侧采集速率，千分之一FPS|
|g_usb_task_debug.completed_frames|跨Class生命周期累计真实EOF完成数|
|g_usb_task_debug.measured_fps_milli|设备侧EOF完成速率，千分之一FPS|
|g_usb_task_debug.max_sof_gap / coalesced_sof_slots|任务观察到的最大SOF增量 / 合并机会数|
|g_bridge_debug.acquired_frames / submitted_frames / released_frames|Camera取得 / USB接受借用 / Media实际归还数量|
|各任务stack_free_words|历史最小剩余栈元素数量|

Control RUNNING只证明两个Start已ACK，不代表Windows已取流。设备EOF完成速率也不等于Windows显示速率。

**25 FPS是目标，不是实测结论。** 三缓冲保持两块DMA、一块消费者。USB持有一帧期间，其他完成帧可能被丢弃。本轮33ms模拟采集源在模拟1000ms内得到31次采集完成、15次设备EOF、15次丢帧；这是模拟条件下的吞吐示例，不是实板性能。未为追求25修改采集时序、缓冲算法或复制整帧。

## 6. 验证结果

新增专项按02A→02B→02C→02D顺序通过后继续；最终统一复跑97/97 PASS，源码/链接/依赖审计38/38 PASS。详细逐例结果及编译命令在`Tests/NativeRuntimeStage02/`。

|正式完整重编|Error|Warning|Code|RO|RW|ZI|
|---|---:|---:|---:|---:|---:|---:|
|CM7 TEST|0|0|83766|2742|892|40412|
|CM7 UVC|0|0|83766|2742|892|40412|
|CM4|0|0|20350|914|636|2580|

实际编译后的配置字节已分别核对：两个CM7配置均为160×120 YUY2、25 FPS，输出类型分别TEST和UVC。

历史回归实际执行1044项：658 PASS、386 FAIL。对`4d463ba`独立导出的基线执行相同旧测试，失败编号和首个失败行完全相同：

|继承失败|数量|根因|
|---|---:|---|
|Streaming/EP1/Probe/Cancel/Contract/Lifecycle|228|旧夹具未先传入视频配置，再注册/查询UVC|
|Payload|2|仍要求513B非法，而基线已支持1023B|
|USB Core|3|仍要求Initcall自动连接，与阶段01约束冲突|
|DCMI Replace|45|仍要求关闭FIFO，基线已使用FIFO|
|BSP/Media联合旧用例|108|同类旧FIFO初始化断言先失败|

新增未执行134项：VideoConfig28、旧单线程Bridge35、Native01 56、Native01Usb15；夹具仍依赖旧桥接入口、私有上下文或缺少新增依赖，编译/链接不兼容。原LegacyP0 11项仍因已移除接口未执行。没有修改或放宽历史断言，也没有用新增97项替代旧测试计数。

## 7. 修改清单

|文件|修改原因|
|---|---|
|Application/Tasks/camera_task.c/.h|Camera唯一所有者、命令/ACK、故障与采集统计|
|Application/Tasks/usb_task.c/.h|TEST/UVC唯一任务侧所有者、事件发送、取消归还与EOF/SOF统计|
|Application/Tasks/control_task.c/.h|BOOT、启动/停止命令和ACK协调、超时及原故障保存|
|Application/app_messages.c/.h（新增）|五个静态队列、角色检查、非阻塞消息交付|
|Application/app_events.c/.h|采集普通C回调适配和任务所有者检查|
|Application/app_tasks.c|在创建业务任务前准备静态消息队列|
|Application/app_main.c/.h|允许TEST/UVC准备；不提前启动业务|
|Application/app_uvc_bridge.c/.h|替换单线程桥接，建立两套所有者账本和四阶段租借协议|
|Application/video_config.c|默认UVC及TEST冷启动编译开关|
|BSP/bsp_dcmi.c/.h|只新增异常通知绑定与锁存后的通知，不改硬件算法|
|Media/Inc/camera_capture.h、Media/Src/camera_capture.c|只新增进度回调和一致统计快照；轮换后通知|
|Platform/usb_device.c/.h|补齐SOF/Incomplete/发送失败通知及上下文说明|
|Service/usb_mode.c/.h|安全停止后回到可启动IDLE，失败保留ERROR|
|Service/usb_uvc_class.c、Service/usb_uvc.h|累计真实EOF计数及任务上下文说明，协议算法不变|
|MDK-ARM/h7_mac.uvprojx|CM7加入消息源文件；CM4保留禁用Native组|
|Tests/NativeRuntimeStage02/、本报告|专项、历史对照、构建、架构审计及实板待办|

以上生产路径均位于CM7，表中Application等路径相对CM7。完整机器清单见测试目录的`modified_files.json`。用户已有描述符头文件注释、IDE调试设置、优化级别/分组调整、output和tmp全部保留，不加入提交。

## 8. 尚未验证及剩余风险

1. 没有下载本阶段固件到实板，没有Windows枚举、真实图像、USB带宽、实际FPS、长时丢帧和停止恢复结论。
2. 模拟器执行真实内核/队列，但跳过首个SVC并手工选择任务；不能证明真实PSP/FPU上下文切换、NVIC抢占、WFI唤醒及最坏延迟。Port和睡眠时钟配置保持基线不变。
3. 原三缓冲的长期借用会限制输出吞吐，25 FPS需实测。SOF合并或延迟、SDRAM争用及CT/NDTR切换窗口仍需实板确认。
4. 持久Abort/Release/Device Stop失败时保持所有权并进入错误/等待状态，不自动强制回收。Device ERROR没有擅自重置，可能需要另行定义生命周期恢复策略。
5. 历史386项失败及134项新夹具不兼容尚未整改；不能宣布全部历史回归验收通过。
6. TEST仍执行原Initcall硬件准备；本轮验证的是独立USB TEST数据面行为，没有增加“跳过所有相机硬件准备”的模式。

7. 本轮没有为IPC和其他原Framework后台服务新增Native运行任务，也没有恢复Framework_Run；不能把视频链路接入视为全部后台服务迁移完成。

实板步骤与取流命令见测试目录README。审核前不合并main。
