# 阶段08C FatFS最终验收报告

## 基线与交付边界

基线：74ceab6ac9bcf70c34b25877fabbfe89b6a44c82。开发分支：codex/stage08c-fatfs。仅交付08C，不合并main，不开发08D。

官方ChaN FatFS R0.16及官方补丁1、2，保留原许可证及UPSTREAM.json哈希记录。FatFS不是ST自研文件系统。未修改官方ST USB Core、HAL/LL及FreeRTOS。

## 软件验证

- 真实storage_fs.c、diskio.c、sd_block.c及官方ff.c以ARMCC5编译，Unicorn运行，介质在隔离RAM中模拟；54场景分别O0/O2，共108/108 PASS。
- SD块设备回归556/556 PASS。原adapter_test.c、stress_test.c只新增当前任务桩和RAW所有权申请，原断言不删除、不放宽。
- 现行IPC、OLED、USB、Camera等14套有效回归1899次执行通过，逐套见retained_regressions.json。历史不兼容测试维护记录保留，不宣称全仓历史测试全部通过。
- 最终CM7/CM4完整重编结果见build_results.json及两核rebuild.log；只在日志明确0 Error、0 Warning时验收构建通过。
- 专项覆盖FAT12/16/32、分区/容量边界、GPT/exFAT拒绝、无卡、短读、写满部分写、超时、同步/关闭失败、掉卡、句柄失效、访问所有权、恢复及授权检查。

## 真实硬件验证（用户操作并反馈）

用户确认下列项目符合预期，不以Mock结果替代：

1. 验收镜像正确运行，ready=1、初始state=0；先前符号/固件不一致已通过明确下载验收镜像解决，未据0xA5填充值认定栈溢出。
2. request=1：真实识卡、只读挂载、卸载成功，state=2、result=0、cleanup_result=0。
3. Watch显示MBR第1分区，first_lba=0x400（1024），sector_count=0x4000（16384），卷范围[1024,17408)，大小8MiB；FatFS实际识别filesystem=1（FAT12）。分区类型标记不能代替实际文件系统识别。整卡容量与卷容量分别管理。
4. CID：9F544930 / 30303030 / 00000189 / BD013CBA。写入前由用户从当前Watch复制整卡容量、CID及卷范围到expected字段，再设置明确写入确认值。
5. request=2：新建测试文件，写入131109字节（128KiB+37），同步、关闭、重开、读取和逐字节/CRC/边界检查均通过。用户确认state=2、result=0、cleanup_result=0、bytes_written=bytes_read=131109、write_crc=read_crc=0x42A20526、buffer_valid=1。
6. 文件采用CREATE_NEW，已有同名文件不覆盖，不自动删除文件，不自动格式化或分区。未对实卡注入写满、热拔卡等破坏性故障。

上述实板结果是用户反馈和提供的Watch截图，不是本机自动采集的硬件日志。电脑端独立读取测试文件未提供证据，列为未执行。此前08B压力/写扇区验收不代替本阶段长期文件系统验收。

## 未完成及风险

- 突然断电一致性、掉电时文件/目录/FAT损坏恢复：未实板验证；f_sync不是掉电事务保证。
- 长期稳定性、持续大量文件操作及视频并行性能：尚未完成实板量化验收。
- JPEG图像保存、MSC模式与FatFS实际切换：未开发；本阶段仅提供互斥访问基础，不宣称实现MSC。
- 独立任务唯一所有者；未接入实际保存业务。文件操作的总体时间可能受FAT扫描/分配影响，底层每次请求有超时，不宣称整文件操作硬实时截止。
- GPT、exFAT、长文件名不支持；无RTC时使用固定日期，不代表真实拍摄时间。
- 快速拔插若未被检测采样观察到，不构成新介质会话证明；显式恢复和重新挂载刷新卡身份。

## 输出恢复与清理

配置及原有AXF/HEX/MAP在重编前已备份，详见output_restore.json。本地磁盘配置已与验收前基线一致，因此不进行无必要的覆盖重写。

正式CM7输出：MDK-ARM/h7_mac_CM7/h7_mac_CM7.axf、h7_mac_CM7.map。原CM7 CreateHexFile=0，保留该用户设置；不把目录里可能存在的旧HEX当作此次新产物。
正式CM4输出：MDK-ARM/h7_mac_CM4/h7_mac_CM4.axf、h7_mac_CM4.hex、h7_mac_CM4.map。

验收固件/临时构建输出在最终审计记录镜像哈希后删除；保留独立测试源码、构建脚本、日志及结果。测试镜像需要时可重建，正常工程不引用它。删除范围清单见cleanup_results.json。正式构建不含验收任务、专用校验缓冲、SdTest、压力任务或历史测试宏。

## 修改范围与兼容性

- Platform新增storage_fs.c/.h、diskio.c、fatfs_diskio.h、ffconf.h；sd_block.c/.h增加显式访问租约、状态、CID和同步接口。
- 新增官方FatFs目录、独立Tests/FatFsStage08C及设计文档。
- Keil仅新增CM7文件/包含路径及CM4显式排除FatFs编译单元，保持原有Target和用户本地设置；CM4生产源码零修改。
- Camera、USB、IPC、OLED、SDMMC/IDMA BSP、Scatter、公共时钟、任务启停路径无生产修改。
- 两个旧测试夹具为新增租约契约做最小准备调整，其余历史测试修改不纳入本次提交；用户已有UsbCore日志等原样保留。

提交SHA及远端一致性由最终交付消息提供，避免报告自引用提交哈希。最终stage08c代码差异以本分支相对74ceab6为准。
