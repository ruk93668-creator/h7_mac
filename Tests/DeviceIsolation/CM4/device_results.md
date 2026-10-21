# 模拟设备生命周期验收

范围：当前CM4 Device实现；ARMCC5/C99编译，Unicorn ARM仿真，无实际外设。每个用例使用全新仿真状态。

| 测试 | 输入 | 预期 | 实际 | 结果 |
|---|---|---|---|---|
| 完整生命周期 | 注册 camera_sim→查找→Init→Start→Stop | 返回0；找到原对象；UNINIT→READY→RUNNING→READY；回调各1次，顺序123 | 最终READY; 回调次数Init/Start/Stop=1/1/1; 顺序=123; 链表检查=2; 四次操作结果=0/0/0/0; Find返回原对象=True; 全部预期断言通过 | Pass |
| 注册、查找及重复项 | 注册两个设备；查询不同字符串地址的同名设备、缺失名称；重复对象/同名注册 | 正确返回原对象或NULL；重复返回-3；链表保持2节点 | 最终UNINIT; 回调次数Init/Start/Stop=0/0/0; 顺序=0; 链表检查=2; 全部预期断言通过 | Pass |
| 非法参数 | NULL设备、NULL/空名称及NULL/空查找名称 | 操作返回-1；查找返回NULL；无回调、无节点 | 最终UNINIT; 回调次数Init/Start/Stop=0/0/0; 顺序=0; 链表检查=1; 全部预期断言通过 | Pass |
| 未注册及对象身份 | 操作未注册对象；操作与已注册设备同名的另一个对象 | Init/Start/Stop均返回-2；不调用生命周期回调 | 最终UNINIT; 回调次数Init/Start/Stop=0/0/0; 顺序=0; 链表检查=1; 全部预期断言通过 | Pass |
| 状态顺序及重启 | 未初始化Start/Stop；重复Init/Start/Stop；停止后重新Start/Stop | 非法顺序返回-4；合法回调顺序12323，最终READY | 最终READY; 回调次数Init/Start/Stop=1/2/2; 顺序=12323; 链表检查=1; 全部预期断言通过 | Pass |
| 缺失生命周期回调 | 在对应合法状态下使用NULL lifecycle或缺失init/start/stop | 返回-7；保留原状态；只执行存在的合法回调 | 最终RUNNING; 回调次数Init/Start/Stop=1/1/0; 顺序=12; 链表检查=1; 全部预期断言通过 | Pass |
| Init回调失败 | 模拟init返回IO=-9 | 原样返回-9，进入ERROR，拒绝后续生命周期操作 | 最终ERROR; 回调次数Init/Start/Stop=1/0/0; 顺序=1; 链表检查=1; 全部预期断言通过 | Pass |
| Start回调失败 | 模拟start返回BUSY=-5 | 原样返回-5，进入ERROR，拒绝后续生命周期操作 | 最终ERROR; 回调次数Init/Start/Stop=1/1/0; 顺序=12; 链表检查=1; 全部预期断言通过 | Pass |
| Stop回调失败 | 模拟stop返回TIMEOUT=-8 | 原样返回-8，进入ERROR，拒绝后续生命周期操作 | 最终ERROR; 回调次数Init/Start/Stop=1/1/1; 顺序=123; 链表检查=1; 全部预期断言通过 | Pass |
| 首次注册状态 | 使用READY/RUNNING/ERROR状态进行首次注册 | 返回-4，不入链表，不调用回调 | 最终ERROR; 回调次数Init/Start/Stop=0/0/0; 顺序=0; 链表检查=1; 全部预期断言通过 | Pass |

结果：10/10 Pass。

正常链路：Register→Find→Init→Start→Stop，最终状态为READY，设备仍保持注册。
测试直接包含实际CM4 device.c，仅替换生命周期为模拟回调，并提供隔离strcmp桩。
每次链表检查验证头尾关系、节点数量及next/prev双向一致性。
当前实现对任何非零生命周期回调结果都进入ERROR，包括BUSY；这属于观察到的现行策略。
本报告仅描述隔离仿真；Keil Target构建另见构建日志，未进行上板测试。
