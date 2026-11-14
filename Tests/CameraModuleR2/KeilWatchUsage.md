# R2 Keil Watch 手动验收入口

代码基线：BASEPRI修复 `1c97d7cca0c19927f4405d2abd34e5cc918fcd51`。
独立分支：`test/r2-keil-watch-control`。本入口不是正式业务功能，不合并main。

## 1. 必须使用独立验收镜像

原 `MDK-ARM/h7_mac.uvprojx` 不包含入口，正式CM7没有 `g_r2_hw` 是正确结果。

生成/重编入口：

```powershell
python Tests/CameraModuleR2/run_watch_build.py
```

脚本先重编正式CM7、CM4，再生成唯一独立验收工程：

- 工程：`D:/32/STM32H7/h7_mac/output/CameraModuleR2/watch_build/h7_mac_r2_watch.uvprojx`
- Target：`h7_mac_CM7_R2_Watch`
- 验收AXF：`D:/32/STM32H7/h7_mac/output/CameraModuleR2/watch_build/firmware/h7_mac_CM7_R2_Watch.axf`
- 配套CM4：`D:/32/STM32H7/h7_mac/MDK-ARM/h7_mac_CM4/h7_mac_CM4.axf`

打开生成的验收工程、选择该Target后用Download烧录，再进入Debug；不要只LOAD符号而没有烧录对应固件。生成工程使用绝对源路径和独立输出，不修改原工程、输出目录、文件名或用户Keil设置。工程会随仓库位置重新生成，不提交本机生成工程或AXF。

首次生成会从原CM7本机选项复制调试器选择到独立uvoptx，并清除新副本的旧断点、Watch和初始化脚本；原选项不改，已存在的验收uvoptx不会被覆盖。若探针型号改变，检查独立工程Debug/Utilities选择。

它只增加 `APP_R2_HW_ACCEPTANCE_ENABLED=1`、验收源文件和独立输出。CM4代码不变。若想恢复正常固件，烧录原正式CM7 AXF。

## 2. 开始操作

1. 保持USB数据线接电脑，完成枚举；未插USB会触发现有5秒配置超时，导致旧Control协调STOP，与Watch入口无关。
2. 完整断电启动，在Watch添加 `g_r2_hw`。`ready=1` 表示Control合法任务上下文已经轮询邮箱，不表示Camera/USB/Storage业务就绪。
3. 等待视频及系统正常启动后再测试，Storage使用已确认可写的SD卷。新固件不自动录像或抓拍。
4. 暂停、填写command，最后填写submit_seq，然后立即恢复运行。也可使用已确认支持的实时Watch写入；任何时候都要先参数后序号。
5. 等待processed_seq等于submit_seq，再提交下一条。每次序号严格加1，从1开始，不回绕。不要直接改processed_seq/ready等输出或业务私有状态。

Control复用原100ms事件等待周期，通知可提前唤醒；实际响应还受任务调度影响，不承诺严格100ms完成。操作正常不需要断点、PC函数调用、修改CPU屏蔽寄存器或任务句柄。

停核太久会使USB主机超时，故障注入/暂停结果不能代替无断点连续运行验收。

## 3. 命令表

| command | 操作 | 实际调用 |
| ---: | --- | --- |
| 1 | 系统START | ControlTask_RequestStart |
| 2 | 系统STOP | ControlTask_RequestStop |
| 3 | 开始录像 | App_StorageStartRecording |
| 4 | 抓拍一张JPEG | App_StorageRequestSnapshot |
| 5 | 停止Storage | App_StorageStop |
| 6 | Storage恢复 | App_StorageRecover |

系统START/STOP保留旧Control的协调策略，不是USB/Storage自主生命周期；不实现C2.2B。

## 4. 依次操作示例

以下假设刚复位，processed_seq=0。若已经执行过请求，使用当前processed_seq+1，不重新从1开始。

### 4.1 系统STOP

```c
g_r2_hw.command = 2
g_r2_hw.submit_seq = 1
```

恢复后查看：

```c
g_r2_hw.processed_seq == 1
g_r2_hw.processed_command == 2
g_r2_hw.dispatch_result == 0
```

这只证明STOP请求入队。接着核对USB Return/ACK、Media完全停止、Device READY、Bridge Idle、Control匹配ACK并进入IDLE。播放器因USB主动断开报告设备错误属于该操作预期，需要区分真正停止失败。

### 4.2 系统START

```c
g_r2_hw.command = 1
g_r2_hw.submit_seq = 2
```

等Control完成Camera/USB START，确认真实新帧，再重新打开播放器。系统处于ERROR时原策略可能拒绝START；不能改状态强制启动。

### 4.3 开始录像

```c
g_r2_hw.command = 3
g_r2_hw.submit_seq = 3
```

确认 `g_storage_status.state=1`，saved_frames持续增加；检查result、cleanup_result及SD卷状态。入队成功不等于挂载、创建文件或写入已经成功。

### 4.4 停止录像

```c
g_r2_hw.command = 5
g_r2_hw.submit_seq = 4
```

等待文件正常Finish/Close/Unmount，查看result=0、cleanup_result=0；还需pending_returns=0、队列为空、权威槽位和SD租约全部归还。Storage IDLE单独不能作为完成证明。

### 4.5 抓拍

确认录像已停止并清理完成，Camera仍在正常采集：

```c
g_r2_hw.command = 4
g_r2_hw.submit_seq = 5
```

观察抓拍完成、saved_files增加和真实JPG文件；若录像仍运行，保留原业务拒绝行为，不由邮箱强制切换。非JPEG模式不能生成伪JPG。

### 4.6 Storage恢复

只在确有Storage故障且需要恢复时执行：

```c
g_r2_hw.command = 6
g_r2_hw.submit_seq = 6
```

观察原Storage恢复结果；恢复成功不证明此前录像保存成功，也不重启Camera硬件。

## 5. 邮箱字段与错误

| 字段 | 写入者 | 含义 |
| --- | --- | --- |
| ready | Control适配器 | 已有合法任务轮询入口 |
| command | 用户Watch | 本次操作码 |
| submit_seq | 用户Watch，最后写 | 提交序号 |
| processed_seq | Control适配器，最后发布 | 该请求已完成分类和一次投递尝试 |
| processed_command | Control适配器 | 冻结的本次操作码 |
| dispatch_result | Control适配器 | 公共接口返回值，绝不是业务完成ACK |

- 0：公共请求接口受理/入队，继续检查原业务完成证据。
- -1：未知操作码，未调用公共业务入口。
- -4/-5/-6等：保留原公共接口拒绝、BUSY或队列满结果，不自动重发。
- -10：序号跳号，未执行该操作。这个输入序号已被消费，下一笔使用processed_seq+1。
- 相同或更旧序号不再次执行；零不提交。达到UINT32_MAX后必须正常重启验收固件，禁止回绕。
- 队列满/失败也只尝试一次；重试应使用新的序号。
- 读输入发生变化时暂不消费；下一次合法轮询重读。用户必须遵守一次一个未处理请求，邮箱不是批量可靠队列，不能连续覆盖尚未处理的输入。
- 私有序号才是去重依据，修改processed_seq不能重新执行旧请求。不要把本验收序号当成生命周期request_id、stream_epoch或lease_id。

## 6. 不改变的业务边界

验收适配器只在ControlTask原Process和状态发布之后调用现有请求接口。CameraTask仍负责Media释放，UsbTask仍负责UVC，StorageTask仍负责FatFS。没有新任务、队列、Timer或动态内存，不读写BSP/HAL或共享帧指针。

正式构建默认APP_R2_HW_ACCEPTANCE_ENABLED=0，不包含验收头和调用；HardwareControl.c被正式误加入时会编译报错，而不会静默开放入口。

**录像运行时先显式Storage STOP、等待文件及租约排空，再系统STOP。** 本入口不补全旧全局STOP缺失的Storage封尾协调，不新增自动清理/恢复策略。直接系统STOP导致录像无帧超时的既有风险仍需独立记录。

此入口不提供START失败或BSP停止失败注入；继续按HardwareAcceptance.md区分调试器模拟返回错误与真实硬件故障。

## 7. 实板记录表

| command/submit_seq | processed_seq/result | Control状态及命令ACK | Storage文件/清理结果 | USB帧归还 | Media完全停止 | Device状态 | 最终结论 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 待填 | 待填 | 待填 | 待填 | 待填 | 待填 | 待填 | 未执行 |

开始/停止、录像、抓拍和恢复均待用户实板验证。软件测试和编译不等于实板通过。
