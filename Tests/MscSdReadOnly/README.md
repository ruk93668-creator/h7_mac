# MSC-4 只读验收工具（验收未通过）

本目录对应基线 `57120051270dd18c5aed834f1c5857f0e4090725` 的只读验收。结果范围与阻塞见 `Docs/msc-4/MSC-4_SD_ReadOnly_Hardware_Acceptance.md`。工具不应当作为无人值守硬件回归脚本整体执行。

- `build.py`：读取当前用户工程，产生临时构建工程，显式设置只读宏，保存预处理结果与固件；结束时核对原工程字节。`build_results.json` 为已执行构建记录；后续补充核对见 `source_audit.json`。
- `discover.ps1`：按 USB PnP 父链识别 VID1209/PID0001，不固定假设盘符或物理盘号。
- `windows_read.py`：有限 SCSI DATA IN 命令白名单，要求先有初始化快照门禁与唯一设备身份。仅管理员运行。提交的结果不能作为下一块卡或下一次启动的门禁；新测试应重新采集初始化证据和设备身份。
- `copy_files.py`：读取卡内现有文件，仅将副本写入电脑 `output/MSC4-readonly/copies/`；独立原件校验未完成。
- `debug_manifest.py` / `decode_snapshot.py`：按指定 ELF 的 DWARF 产生诊断范围，解析 Keil HEX386。必须保证所烧镜像与解析 ELF 相同。默认的初始化门禁不判断 BOT 生命周期或所有 USB 缓冲归还；必须另外审查相关状态。
- `analyze_capture.py`：输出 BOT/SCSI 元数据，不输出卡数据。预计2扇区批次数是源码推断；不作为实际后端调用计数。会话间 Tag 可复用，不能仅按 Tag 跨会话关联。
- `active_disconnect.py`：最多180秒的64KiB只读循环，配合人工拔 USB 数据线。`read error/disconnect` 仅是脚本错误分类，不能单独证明实际拔线。失败 IOCTL 的长度、状态、哈希不得作为成功读取证据。
- `controlled_protocol.py`：有限读取与 READ phase-error 模式。**本轮 `write_protect` 未执行，自动审批拒绝了撤掉调试防护后发送 WRITE 的请求。禁止将历史日志当作当前硬件保护证明；不要运行此模式，后续需要新鲜镜像、现场保护与审批条件重新审核。**

`results/` 只保存本次协议元数据和过滤后的诊断状态；不是完整原始抓包。原始SRAM、抓包、固件、MAP和文件副本留在本地输出目录，不提交Git。构建日志已移除许可证相关行。Python语法检查不代表硬件异常路径验证通过。
