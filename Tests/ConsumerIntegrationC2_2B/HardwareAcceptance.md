# C2.2B 实板验收清单（未执行）

先阅读实施验收报告：本阶段是Draft，RELEASE真实证明未接入，不能按完整停止/重启闭环验收。

## 前置条件

使用本分支正式CM7及配套CM4，完整断电启动。保留用户视频配置；Storage需要JPEG，非JPEG沿原接口返回NOT_SUPPORTED。本轮不自动格式化或改分区。仅使用已确认可写的FAT卷，文件仍为不覆盖的8.3名称。

正式源码不存在历史g_r2_hw/g_storage_debug.request邮箱，本轮不恢复。请求仍由合法任务上下文调用ControlTask_RequestStart/Stop，以及App_StorageStartRecording/RequestSnapshot/Stop/Recover；不要写诊断、私有资格或帧凭据来模拟成功。若需要纯Watch触发，另行准备独立验收构建，不混入生产固件。

## 观察变量

- g_camera_lifecycle_debug：初始化和资格运输结果；process_result=0不是所有资源完成证明。
- g_usb_camera_consumer、g_storage_camera_consumer：phase/session/generation/request_id/granted/result。
- g_camera_task_debug、g_usb_task_debug：业务状态、实测FPS、帧数、栈余量。
- g_storage_status：文件状态、结果/cleanup_result、saved_frames/files、pending_returns、return_result和栈余量。
- 原Bridge/IPC诊断：原Return/ACK、最近故障、心跳/超时及任务栈。

phase=3/granted=1代表本消费者已收到授权。phase=4/granted=1代表释放还在等待，即使result=0也不能宣称完成。空闲/等待中的Flush返回码不能单独判成业务故障。

## 测试表

| 项目 | 操作/检查 | 当前应如何判断 |
| --- | --- | --- |
| USB-only | UVC冷启动，连接Windows并打开视频 | QUERY/ACQUIRE后USB启动；记录真实图像/FPS/栈，未执行 |
| SD-only | NONE+JPEG冷启动 | 冷启动Camera停止；Storage请求后再启动采集和文件，未执行 |
| 双路 | 在USB视频运行时请求录像/抓拍 | 两个独立grant；原始帧租借保持，未执行 |
| 双路冷请求 | 合法调用者在启动期同时提交两路请求 | CameraTask逐条准入，两路授权且一次资源启动；记录错误，未执行 |
| USB业务STOP | 双路运行时请求USB停止 | USB取消/Return/ACK完成，SD继续；USB资格释放保持等待，Camera继续，未执行 |
| Storage STOP | 双路运行时请求Storage停止 | Finish/Close/Unmount及实际归还独立核对，USB继续；资格仍等待，未执行 |
| 同路再次START | 业务已停但RELEASE等待时重启 | BUSY是本阶段安全边界，不能清状态强行恢复，未执行 |
| Reset/断连/挂起 | 观察USB总线事件 | 不直接清grant，明确STOP是另一条业务命令，未执行 |
| 文件错误 | 无卡/掉卡/写失败等受控场景 | 保留真实文件/清理结果；不得提前释放帧或停止另一路，未执行 |
| 并行持续运行 | 真实视频+SD至少记录一段稳定运行 | 分别记录采集/USB/保存FPS、丢帧、IPC及栈；没有预填实测数值 |

每项单独记录固件SHA、配置、开始/结束时间、变量前后值及结论。每个独立场景从完整冷启动开始，不以复位代替正在进行的文件收尾。

## 本阶段不能宣称通过的项目

真实资格释放、最后消费者退出后Camera硬件停止、主动Shutdown、完整重复启停与故障恢复属于C2.2C。Core直接批量冷QUERY仍存在负例，不能绕过CameraTask准入规则。软件及静态栈结果都不是实板故障验收。
