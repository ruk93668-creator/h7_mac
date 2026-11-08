# 每秒栈余量扫描与USB采样清理

Camera、Usb、Control、IPC任务各自使用任务私有本地Tick，每经过1000ms最多扫描一次。SD测试的普通结果与压力路径共用同一个任务私有计时，每秒最多一次。没有新增任务、定时器或唤醒；任务下一次执行时才更新，长时间阻塞后不补扫。第一次有效扫描前Watch栈余量可为0，之后仍是FreeRTOS历史最小剩余Word数，不是瞬时空闲空间。

原20ms SD轮询、压力代码和缓冲、请求处理、C组开关、任务优先级、BOOT及USB/Camera流程保持不变。只改变栈统计频率；SD计时测试桩补充Platform_TimeGetMs，不放宽原断言。

USB固件采样点、采样宏、IncludePath、记录器源文件注册和AXI专用区域均删除。旧HEX、PC解析器及历史结果保留，固件不再提供g_usb_iso_timing，旧导出指令不适用于此版本。

## 验证

- `python Tests/TaskStackSampling/run_tests.py`：提取实际五个模块的节流代码，以ARMCC O0/O2编译并在Unicorn执行，共10/10通过；检查999/1000ms、重复时刻、延迟不补扫、Tick回绕和结果保留。
- 原SD556/556、Native USB11/11、IPC任务41/41通过。没有放宽原业务测试断言。
- CM7、CM4增量构建0 Error、0 Warning；不是全量重编。
- audit.py是本次相对d1654c9的范围审计和实际AXF检查，共18/18通过，不作为未来任意版本的通用CI。确认其他代码去除本轮节流与历史采样后相同，HAL/LL、官方Core、Media、Device、Camera和CM4不变，SD C组配置及两个AXI缓冲区不变。

结果见verification_report.json、results.json、audit_results.json和两核构建日志。没有重新执行全仓全部历史套件。

## 实板复测

在原h7_mac.uvprojx打开h7_mac_CM7 Target，确认USB采样宏已删除，APP_SD_BLOCK_TEST_ENABLED=1、APP_SD_BLOCK_STRESS_TEST_ENABLED=1、APP_SD_BLOCK_WRITE_TEST_ENABLED=0保持不变。重新构建并烧录，完整断电启动。

保持g_sd_block_test.request=0，不读写SD，使用同一视频配置和播放器，观察Camera与USB实际FPS、USB完成帧数及各任务stack_free_words；栈余量约一秒更新一次。记录启动后1分钟和5分钟USB帧率。本轮待实板验证，不能根据软件PASS宣布已恢复25FPS，M-V长期延迟问题仍不在本轮范围。
