# H1 重试准备

上一轮复合镜像下载的 FAIL 和原日志保留。本轮软件前置核验 **PASS（13 项）**，实板 Flash Verify 待操作者执行。

1. 退出 Keil 中正在运行的调试会话，确保调试器连接到本轮 V1 板。
2. 双击本目录 [H1_Run_CM7_ONLY.cmd](H1_Run_CM7_ONLY.cmd)。它只读取 H0 固化的 CM7 HEX，验证哈希后执行扇区编程。程序逐字节读回 217,992 字节并验证。它还比较上一轮涉及的 CM4 第一个 128 KiB 扇区在本次操作前后的内容，结果写入 retry_ 日期目录。出现 VERIFY_OK 时发该目录的 H1_program_verify.json。失败时保存原始日志和 JSON。
3. Flash Verify 核对后打开 Keil 的 h7_mac_ethernet_h1_watch.uvprojx。Watch 1 已预置 MPU/CCR 的五个表达式，Watch 2 已预置网络状态和首次故障。工程关闭了自动编程、Flash Init Script 和自动运行至 main。Debug 初始化使用已核验 AXF 的 NOCODE 符号。
4. 依据 [H1 读回步骤](../H1_Flash_Verify.md)保存实际读回。H2 继续等待 H1 完成。

Keil 工程副本位于 C:/Users/29062/AppData/Local/Temp/h7_q11_04dmg4_u/n5_joint_verified/MDK-ARM/h7_mac_ethernet_h1_watch.uvprojx。新工程隔离编译输出目录。原工程、AXF、HEX、MAP 仍与 H0 哈希一致。

## Keil 显示 failed to execute 空命令时

这是与 Watch 工程禁用烧录入口一致的现象。先确认已退出 Keil 的调试状态。打开独立的 Windows PowerShell，将下列命令一次性粘贴到 PS 提示符。它会直接启动本轮 CM7 下载与读回程序，在 H1_retest 下新建 retry_ 证据目录。

~~~powershell
& "C:\Users\29062\AppData\Local\Programs\Python\Python311\python.exe" -B "D:\32\STM32H7\h7_mac_wk\output\Ethernet\hardware\N5_R2_R3_H0_H1_H2_20261128_171920\H1_retest\h1_cm7_only.py" --program
~~~

使用 VERIFY_OK 和生成的 H1_program_verify.json 作为 Flash 阶段证据。此次 Keil 启动错误已保存到 [记录](H1_Keil_Launch_Error.json)。
