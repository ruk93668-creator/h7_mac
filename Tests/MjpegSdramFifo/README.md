# MJPEG SDRAM FIFO隔离检查

本轮不调用C编译器，不执行可执行测试，不连接实板。`static_results.json`只记录源码审计。

## 源码检查

从工程根目录执行：

```powershell
python Tests/MjpegSdramFifo/static_audit.py --baseline 60c94d0
```

需要Python的pycparser。基线通过参数显式指定；该脚本不依赖本地绝对路径、当前HEAD是否未提交或旧迁移工作树，不作为完整运行时CI。

## 已准备的C测试（14项，未执行）

`fifo_test.c`直接包含正式FIFO和Payload源码，BSP只替换为块回调测试桩。PC在低地址申请独立虚拟内存，不访问真实SDRAM。申请失败返回2并显示NOT RUN，不能统计为PASS。

| 编号 | 输入/操作 | 预期 |
|---|---|---|
| 01 | 启动默认布局 | 五缓冲、FIFO、双丢弃区不重叠，两个DMA目标不同 |
| 02 | 空配置、320宽配置 | 参数/不支持错误，不调用BSP Start |
| 03 | 只有FRAME，没有块TC | 没有READY |
| 04 | 完整块内的真实JPEG样本 | 借出1113B，逐字节与样本相同 |
| 05 | 连续72块、每轮消费 | 至少一帧跨FIFO尾，两段内容正确 |
| 06 | USB长期持有第一帧 | 缺空间写丢弃区，持有内容不变，归还后恢复READY |
| 07 | 不消费READY | 描述队列最多8项，只丢未借出帧 |
| 08 | 同块多帧，归还其中一帧 | 只减少该帧引用，不提前释放整块 |
| 09 | 有完整JPEG但没有FRAME | 先不发布，随后FRAME到达才发布 |
| 10 | APP段含伪FFD9 | 不截断，真实长度仍为1113B |
| 11 | SOF精度非法 | 无READY |
| 12 | 已借出后确认底层Stop | 借出保留，归还后才允许再次Start，重复归还拒绝 |
| 13 | BSP Start返回IO | 保留占用，确认Stop前不能重启 |
| 14 | 两段Payload、逐包完成、取消 | 内容连续，EOF只在末包；确认前不推进，取消确认后释放两段 |

每轮检查块引用=READY+IN_USE+当前解析描述引用，并检查FREE/DMA块无人持有。

用户有MinGW GCC时可自行编译执行（本轮没有执行）：

```powershell
gcc -std=c99 -Wall -Wextra -I Tests/MjpegSdramFifo/stubs -I Common/Inc -I CM7/BSP -I CM7/Media/Inc -I CM7/Service Tests/MjpegSdramFifo/fifo_test.c -o output/mjpeg_fifo_test.exe
.\output\mjpeg_fifo_test.exe
```

Linux需额外`-D_GNU_SOURCE`以使用`MAP_FIXED_NOREPLACE`。不要用关闭assert的构建运行本测试。

这些测试不覆盖真实DMA总线、FE、CT换址窗口、FMC时序和USB主机行为；BSP硬件安全检查仍须用户编译和实板故障注入验证。旧03B重复Snapshot的内部状态断言不能当作新FIFO实现测试；本轮未删除或放宽这些历史断言。

设计和实板步骤见`Docs/Design/MJPEG_SDRAM_FIFO.md`。

## 旧CM7_MJPEG工程链接错误

用户反馈的7个CameraMjpegFifo未定义符号来自旧`h7_mac_stage03b.uvprojx`：有camera_capture.c，却没有camera_mjpeg_fifo.c。
本轮补齐现有临时工程各CM7 Target的源文件条目，CM4的CM7 Media Group继续排除构建；不重新生成或覆盖本地调试设置。

`run_build.py`以后从当前正式工程读取文件列表，并校验FIFO条目。各变体会去掉继承的MJPEG/TEST/单帧开关，再应用自己的开关，避免正式工程选择MJPEG时误污染YUY2构建。
本轮未运行该构建脚本、编译器或Keil。重新打开当前工程后，完整Rebuild应出现`compiling camera_mjpeg_fifo.c...`；是否编译通过由用户反馈。
