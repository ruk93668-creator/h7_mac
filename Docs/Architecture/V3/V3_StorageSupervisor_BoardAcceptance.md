# ApplicationV3 Storage JPEG/AVI 与 Supervisor 集中实板验收

源码起点 `d6f9b6fea2c72c2ec2e167f067913565c2004cc3`，分支 `codex/application-v3-media-usb`。S0 仅加入独立板测编排和诊断。普通板测一次只运行一个 Service；录像板测使用现有 ControlTask 的 `AppV3Supervisor_Set/Query/Read/Recover`。BOOT 不打开外设或可写卷，Watch 通过静态邮箱提交命令。Camera/Storage 公共接口、JPEG FIFO 所有权、IPC、FreeRTOS 任务配置和产品默认配置沿用原实现。

| 阶段 | 状态 | 已知证据或条件 |
| --- | --- | --- |
| S0 软件入口、构建与回归 | PASS | ARMCC5 专项、双核产物、Watch 符号、工具正反对照 |
| S0 实卡分区确认 | BLOCKED | 镜像为候选 MBR FAT 分区 1，等操作者核对实际 SD 和批准写入 |
| S1 实卡录制 | NOT_RUN | Camera JPEG、可写挂载、SD 写入与真实速率 |
| S2 AVI 收尾及所有权 | NOT_RUN | 匹配 RETURN/ACK/CONSUMED、实际 Finish/Close/Unmount、Windows 文件结构与播放 |
| S3 Supervisor 实板 | NOT_RUN | 局部和全局启停、无复位重启、Query、故障隔离与显式恢复 |
| S4 稳定性与性能 | NOT_RUN | 循环、实际文件、计数、延时、栈水位、I/O 诊断 |

## S0 软件证据

ARMCC5 5.06u7 O0/O2 各通过 2/2 录像邮箱专项和 73/73 默认套件。A1 物理租约、A2 Dispatcher、A3 UVC Reader 和 Raw Service、原八组板测软件配置均通过，两档合计 356/356，另有本地 A4.1 编排 10/10。验证了 BOOT 无自动启动、非 Control 轮次不能取走命令、空或不匹配分区拒绝、Camera 先于 Storage 启动、栈采样入口。原 ABI 52 B AppMessage、16 B ServiceCommand、80 B IPC。只读 AVI 工具接受合成的正确索引，拒绝错误索引，未检查真实录像。

| Keil Target | Code / RO / RW / ZI（B） | 结果 |
| --- | --- | --- |
| CM7 产品 | 137338 / 3982 / 1292 / 101028 | 0 Error、0 Warning |
| CM7 录像测试 | 139602 / 4234 / 1296 / 101504 | 0 Error、0 Warning |
| CM4 | 26302 / 1438 / 936 / 3576 | 0 Error、0 Warning |

录像宏为 `APP_VIDEO_MJPEG_MODE=1, APP_VIDEO_TEST_MODE=0, APP_V3_RECORD_BOARD_TEST=1, APP_STORAGE_PARTITION=1`。产品 Map 无板测 Watch 符号。CM4 HEX 与冻结版 SHA256 一致。证据在 `output/ApplicationV3/StorageSupervisor/`，镜像与 Map 位于 `S0-Keil-20261123-204241/`，回归清单为 `S0-Regression-20261123-204659/verification.json`。用户的 Keil 工程、选项及构建前活动的 CM7 AXF/Map 已恢复。A4.1 镜像仍保存在其独立归档。录像 AXF SHA256 为 `E830D3B366CE218398CF27C4767A1A90D273BEDD92E71062EF43B12A4290FB36`。

## 烧录和 Watch

先确认插入的实际 SD 卡和允许写入的 MBR FAT 分区。Windows 只读查询可用 `Get-Volume -DriveLetter F`、`Get-Partition -DriveLetter F`，将 F 换成实际盘符，再按返回的 DiskNumber 用 `Get-Disk -Number N` 确认为同一物理 SD 和 MBR。仅当确认分区 1 是批准测试分区时才用此镜像录像；若批准的是其他分区，先改板测宏并重建。使用 FAT16/FAT32，保留已有数据并检查待用文件号尚未出现。

1. 退出 Keil Debug。把归档 `Recording/h7_mac_CM7.axf` 和对应 Map 复制到原 `MDK-ARM/h7_mac_CM7/`（保留归档，CM7 AXF 哈希应与上表相同）。用原工程先选择 CM4 下载，再选择 CM7 直接 **Flash → Download**。重新 Build 会用原 Keil 配置覆盖板测 AXF。
2. 进入 CM7 Debug，确认链接符号并加入 Watch。暂停时写命令参数，最后写 `command`，随后 Run。依次观察 `issued` 递增、`dispatch_result=0`、`command_pending=false` 与 `supervisor.pending_id=0`。软件接单和 Service 完成分别核验。每次录像都要重新填分区和文件号。
3. S1 先用命令 1 启 Camera，再用 5 查询。确认 `supervisor.camera.hardware.state=3`、`flags & 1`、完成数大于零。确认分区后写 `approved_partition=1`、新非零 `file_number`，再用命令 2。检查 Storage ACTIVE、FATFS owner、`filesystem.writable`、文件号、两个时刻的 `frames/movi_end`、`first_tick/last_tick` 及 SD/FatFS 状态。文件刚打开后继续移动物体，留下可辨认的画面变化。
4. S2 在仍录像且已有完整帧时发命令 3。确认 Storage READY（1）、Camera ACTIVE（3）、原文件句柄清零、卷诊断有效且 `open_files=0`、`fatfs_owner=0`、IDMA 原状态和结果。保存 `finish/close/unmount_attempted` 与各自 result，四条握手计数应一致。Camera 的 `confirmed` 为开机累计，用本次增量并核对最终 `request_id/session/lease_id`。等待完整停止再取卡，复制 AVI 到 Windows。
5. 用 `python Tests/ApplicationV3/inspect_recording.py <Windows复制的AVI>` 留下 RIFF/movi/idx1 结构和帧数，再用视频播放器观察动态画面。结构验证与可解码、动态画面分别记录。文件存在不单独计为 PASS。

| Watch 符号 | 地址 | 关键字段 |
| --- | --- | --- |
| `g_app_v3_record_command` | `0x20008694` | `approved_partition`、`file_number`、`command` |
| `g_app_v3_record_board` | `0x200086A0` | `configured_partition`、`supervisor`、请求用时、栈水位 |
| `g_app_v3_record_storage` | `0x20006E04` | `hardware`、`frames/movi_end`、文件与 FATFS/IDMA、握手、实际收尾结果 |
| `g_app_v3_record_camera` | `0x20006CCC` | Camera 真正消费的确认次数和最后匹配租约 |

命令 1 启 Camera，2 启录像（必要时自动先启 Camera），3 停录像并保留 Camera，4 全局停止，5 查询 Camera，6 查询 Storage，7 显式 Storage RECOVER，8 采集任务栈余量（单位为 4 B 的栈字）。命令 7 仅在原故障安全边界允许、已保存错误现场且经 Query 确认 uncertain 后使用。所有 Watch 数据在任务继续运行若干轮后再暂停读取，禁止由调试器跨任务调用 Service API。

## S3 / S4 实板留证

S2 正常完成后，从全停状态发 2，录下 Camera 同身份完成先于 Storage 挂载的快照。用 3 停当次录像，检查 Camera 继续 ACTIVE。换新文件号发 2，再发 4，检查 Storage READY 后 Camera 才进入停止。全 READY 后用新号重复 2，无 MCU Reset。使用 5、6 读取原实际状态。一次到达配置的时间或文件上限后用 6 刷新，核对 Supervisor 与自主结束的 Storage 一致。若观察到 STOP 失败，记录 `uncertain/first_fault`、两个 Service 与卷快照，确认 Camera 保留，并按已有契约单独验证允许的显式恢复。

建议十轮使用不同文件号。报告中记录两次采样的时间、Camera 完成和丢帧增量、写入帧和字节增量。单个录像段可用 `1000 × (frames − 1) / (last_tick − first_tick)` 计算实际平均写入 FPS，至少两帧且时间差非零时才有效。`movi_end − 224` 是成功提交的已封装帧字节，最终 AVI 文件字节和索引数量取自复制文件。单段仍受原 4 MiB/60 秒上限，不将多轮分段测试声明为无间断长录像。记录 `finish_ms` 和 Watch STOP 命令耗时（后者含协调轮询），用 8 采样 Camera、Storage、Control 栈高水位。首轮不做写入期间拔卡。

失败时保留前后 Watch、未修补的原 AVI、Filesystem 和 IDMA 结果、租约状态及首错。保持当前现场，停止扩大测试，禁止用超时强制释放、清空租约、自动重启或格式化。
