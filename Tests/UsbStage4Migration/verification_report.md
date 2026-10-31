# USB Stage 4 验收与合并记录

## 已完成状态

2026-10-31，完整USB基础设施成果已提交、快进合并到正式main并推送origin/main。

- 合并前：`98a9f89637b39e3739414c0405ad5f508b0fbda1`
- 正式提交：`dbf3e9ba798e1434796ecc7d1e585018edb9843c`
- 合并与推送后已核对本地main和远端main SHA一致。此处记录当次结果，不表示远端永远停留在此提交。
- 保留Service中的Mode Manager及Keil布局；用户原有未提交修改未纳入提交，已保留。

## 当次实际验收

|项目|结果|
|---|---|
|USB完整回归|204/204 PASS，2984条断言，无跳过|
|EP0 IRQ竞态补充|4/4 PASS，110条C断言及事件顺序检查|
|CM7完整重编|0 Error、0 Warning|
|CM4完整重编|0 Error、0 Warning|
|受保护源文件SHA|1593个文件一致，包括HAL/LL、官方USB Core、UVC及CM4|

测试使用ARMCC5与Unicorn；IRQ补充测试执行真实HAL IRQ函数体及LL，端点操作和W1C寄存器行为由测试桩/模型驱动。未验证实际NVIC异常入口、PHY、Windows枚举或实板竞态时序。

## 生产修改范围

|文件|修改|
|---|---|
|CM7/BSP/bsp_usb.c/.h|PIO逐包暂存、Abort/Close所有权、共享RX保护、EP0旧XFRC清理|
|CM7/Platform/usb_device.c/.h|Core/Class解耦、端点声明匹配、EP0生命周期、错误分域及Class私有数据隔离|
|CM7/Service/usb_mode.c|统一选择默认TEST，不覆盖显式Class，仅响应设备级故障|
|CM7/Service/usb_test_device.c|移除TEST自行注册，由Mode Manager选择|

没有修改HAL/LL、官方ST USB Core、UVC Class/Descriptor及CM4源码，没有实现UVC协议。

## 历史证据与长期测试的区别

原audit.py依赖迁移前未提交差异、旧HEAD、绝对路径和现场快照，只证明当次迁移过程；它不是CI检查，也不能用来验证合并后的任意提交。原脚本原文位于[History](History/README.md)，扩展名为.py.txt，避免误运行或被工具收集。

一次性日志、SHA快照、清单及分层补丁已从当前目录精简；原始证据完整保留在Git提交dbf3e9b，不改写历史。恢复方法、重复执行的回归入口和依赖说明见[目录说明](README.md)。

实板USB枚举、EP0新SETUP与旧XFRC竞态、Abort恢复、并发OUT和PIO/FIFO行为仍待硬件验收。
