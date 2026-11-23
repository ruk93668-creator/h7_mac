# B2-B5 开发检查点（未完成）

日期：2026-11-23。用户已授权目标：手机搜索、连接及测试GATT。

本次加入独立H5、BLE专用heap_4包装和NimBLE NPL草稿，尚未加入Keil生产构建，因此现有B1固件不会因这些文件自动广播。

- H5：建链/SLIP/窗口1/ACK/重传/CRC/背压；8类合约场景通过ARMCC+Unicorn运行。仍需更完整的序号回绕、异常分片、流量与实板验证。
- heap_4：FreeRTOS V10.6.2原文件；通过BLE包装重命名符号和局部动态配置，16KiB私有堆。未改全局FreeRTOSConfig。NimBLE文件才预包含malloc映射配置。
- NPL：静态队列、递归所有权锁、计数信号量、事件队列及callout草稿。锁没有优先级继承；并发/取消竞态、队列唤醒及资源耗尽测试未完成，不作为已验收实现。
- NimBLE：固定nimble_1_8_0_tag，许可证、来源与文件hash保留在UPSTREAM.json。60个编译单元通过独立编译，存在上游警告；未链接，不能据此声称可运行。
- 测试：Tests/BleBringup/compile_results.json与h5_results.json。H5测试链接使用现有scatter，空framework_init产生1条链接警告。
- 用户已有MDK-ARM/h7_mac.uvprojx、uvoptx修改保留，未纳入本检查点提交。

未完成：控制器版本读取/固件选择/分块下载/状态验证，H5到NimBLE transport，Host与I/O任务，GATT读写Notify测试服务，Keil集成与完整链接、内存预算、运行并发测试和实板扫描。B2-B5总体状态IN_PROGRESS，手机发现/连接/GATT全部NOT_VERIFIED。

候选RTL8723BS固件及许可证仅在临时目录完成研究，未加入工程；不得虚报已下载至控制器或已广播。
