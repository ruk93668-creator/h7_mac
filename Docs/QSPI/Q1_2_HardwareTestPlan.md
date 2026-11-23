# Q1.2 实板只读验收准备（未授权执行）

本文件为后续审批材料，不授予下载或实板运行权限。本轮未连接调试探针、未下载、未执行任何 Flash 访问。

## 工程和下载范围

后续人工操作仍仅打开 `D:\32\STM32H7\h7_mac_qflash\MDK-ARM\h7_mac.uvprojx`，选择 h7_mac_CM7；先 Rebuild 并核对0错误0警告。现有用户 Keil 配置保留。QSPI 只在M7使用，不下载或修改CM4。

已验证隔离构建的 M7 AXF 文件负载范围为 `[0x08000000,0x080241D4)`，SHA256见 evidence/resources.json；如未来重建镜像，必须重新核对实际范围及哈希，不将此范围直接套用于其他镜像。128KiB扇区粒度下涉及内部Flash的前两个扇区 `[0x08000000,0x08040000)`。CM4镜像起于0x08100000，本任务不授权覆盖它。

当前本地 uvoptx 记录 STM32H7x_2048.FLM、内部Flash 0x08000000/2MiB；这只证明本地配置文本，不证明未来实际下载范围。获准执行前人工审核 Flash Download 仅内部Flash、按所需扇区擦除，禁止全片擦除和任何外部QSPI加载器；调试自动下载开关也要纳入审核。出现额外地址、外部加载器或CM4覆盖要求时停止。下载范围与回退镜像须单独批准。本轮构建仅调用UV4 -r，不调用下载命令。

## 只读入口

测试实现：`Tests/QspiFlashQ1/hardware/m7_project_test.c:66` 与 `q12_scan.h`。沿用已有静态任务和3072字节栈，没有新增任务；生产 Application 未改，仍可能有原有业务并发活动，后续记录其运行状态。

获准下载/运行并确认板卡、供电、原Q1.1门闩识别完成后：

1. 用符号观察 g_q1_hw_report.complete、id、诊断，要求READY、无FAULT、ID=EF4018，确认HCLK/分频/CR/DCR和TIM6。
2. 在另外取得实板只读执行授权后，才可设置 g_q12_hw_arm=0x51325244。默认0，单次消费；复位前不自动重试。不要沿用旧内存截图的固定结构长度。
3. g_q12_hw_report.phase=1进行全容量4KiB分块扫描，每块先0B参考、后6B，再逐字节比较和累计CRC32；phase=2进行短读及跨页/跨扇区/末端矩阵，phase=3、complete=1表示结束（仍需检查result）。
4. 记录address、confirmed、bytes、blocks、mismatches、non_ff、non_uniform、scan_complete、crc_fast/quad、fast_ms/quad_ms、started/finished和diagnostics。CRC为IEEE反射多项式EDB88320、初值和末异或FFFFFFFF；累计数据顺序为完整扫描后再接短读矩阵，bytes包含重复读取。
5. 每个BSP调用预算1000ms；全容量循环有120秒保护和任务让出，超时保留证据，不自动重启或降频。任何错误停止后续比较；phase结束不等于PASS。

吞吐率分别用有效比对 bytes×1000/fast_ms、bytes×1000/quad_ms 计算，仅在全部读取成功时使用；耗时包括状态检查、分段和抢占，不等于总线理论吞吐。毫秒分辨率不足的短读不单独给出高精度结论。

## 验收与回退

外部逻辑分析仪/示波器另行记录实际CLK与IO0、IO1、IO2、IO3活动，证明1-1-4数据阶段；Mock和两路全FF不能替代波形。确认非均匀有效数据、逐字节一致、CRC一致、完整扫描成功、无未处理诊断，再由人工审核数据完整性；data_integrity_verified字段始终0，入口不会自行宣布验收PASS。

若全FF、大部分FF或缺少足够数据变化，保持NOT_VERIFIED；不得写入测试图案。若120MHz失败，保存原始诊断与波形，不自动降频掩盖。遇供电异常、未知控制器状态、FAULT、BUSY/SUS、QE=0或数据不一致，停止执行并导出报告。恢复需复位及经批准的M7基线固件；禁止擦除W25Q128，禁止触碰CM4。原有数据用途、可擦写区域继续未知，本阶段不需要也不分配授权扇区。
