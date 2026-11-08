# FatFS Stage08C独立测试

完整交付和实板记录见[verification_report.md](verification_report.md)，分层设计见../../Docs/Design/FatFS_Stage08C.md。

## 软件执行

在工程根目录执行：

```powershell
python Tests/FatFsStage08C/run_tests.py
python Tests/FatFsStage08C/run_build.py
```

需要ARMCC5/Keil、Python pyelftools与Unicorn。测试实际编译生产Platform和官方FatFS，不复制文件系统实现。模拟介质仅在RAM，不访问实卡。结果见results.json、commands.json、build_results.json。

## 实板验收重建（非默认正式固件）

```powershell
python Tests/FatFsStage08C/build_acceptance.py
```

脚本临时替换工程文件并finally恢复；运行前关闭Keil避免外部覆盖。产物output/FatFsStage08C/hardware/h7_mac_CM7.axf。该镜像有独立FsAccept任务及CPU校验缓冲，正式工程无引用。必须明确Flash下载该镜像，符号与Flash须匹配，不能只换符号。配套CM4不改。

Watch g_fatfs_acceptance：ready=1后request=1，只读识卡/挂载/卸载；成功state=2、result=cleanup_result=0。复制当前volume.cid[4]、first_lba、sector_count和capacity到expected对应字段，确认允许在当前FAT卷创建测试文件后，file_index选择1..9999未用编号，write_confirm=0x46535752，最后request=2。

新建0:/F08C0001.TST（随编号变化），绝不覆盖旧文件。写入131109B，同步关闭，再重开读回逐字节比较与CRC32。通过条件state=2、result=cleanup_result=0、bytes_written=bytes_read=131109、双CRC=0x42A20526、buffer_valid=1。失败state=3，保留phase和两种错误，request=3为人工恢复，不保证原文件已保存。文件不自动删除；电脑端核验后由用户自行处理。

验收完成恢复正常输出/Scatter并下载正常固件。当前配置已核对回验收前值，见output_restore.json。验收临时镜像已按用户要求清理，保留构建脚本可再生。

## 审计工具说明

audit.py是本轮本地验收工具，依赖tmp基线和清理前验收镜像；结果audit_results.json作为历史证据。它不是独立可复用CI，镜像清理后不要把再次执行失败视为产品回归。长期可运行的软件验证入口是run_tests.py。历史不兼容测试维护记录沿用08B，不删除或放宽断言。
