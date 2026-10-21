# CameraStream隔离状态机测试

真实camera_capture.c直接编译到测试翻译单元；不链接HAL。测试在Unicorn执行ARMCC5生成的ARM机器码。

| 用例 | 输入 | 预期 | 实际 | 结果 |
|---|---|---|---|---|
| 三缓冲初始化地址容量及对齐 / Y8 | 160x120配置 | DMA索引0/1，第三块FREE，无消费者，物理范围不超过128KB | {'fail_line': 0, 'checks': 28, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 0} | PASS |
| 三缓冲初始化地址容量及对齐 / RGB565 | 160x120配置 | DMA索引0/1，第三块FREE，无消费者，物理范围不超过128KB | {'fail_line': 0, 'checks': 28, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 1} | PASS |
| 三缓冲初始化地址容量及对齐 / YUV422 | 160x120配置 | DMA索引0/1，第三块FREE，无消费者，物理范围不超过128KB | {'fail_line': 0, 'checks': 28, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 2} | PASS |
| 空队列查找及获取释放拒绝 / Y8 | 刚初始化 | Find返回2，Get为false，Release返回INVALID_STATE | {'fail_line': 0, 'checks': 23, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 3} | PASS |
| 空队列查找及获取释放拒绝 / RGB565 | 刚初始化 | Find返回2，Get为false，Release返回INVALID_STATE | {'fail_line': 0, 'checks': 23, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 4} | PASS |
| 空队列查找及获取释放拒绝 / YUV422 | 刚初始化 | Find返回2，Get为false，Release返回INVALID_STATE | {'fail_line': 0, 'checks': 23, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 5} | PASS |
| M0首次完成及获取释放 / Y8 | 完成M0 | 替换M0为Buffer2，Buffer0交付，重复Get/Release被拒绝 | {'fail_line': 0, 'checks': 49, 'operations': 1, 'replaces': 1, 'deferred': 0, 'case_id': 6} | PASS |
| M0首次完成及获取释放 / RGB565 | 完成M0 | 替换M0为Buffer2，Buffer0交付，重复Get/Release被拒绝 | {'fail_line': 0, 'checks': 49, 'operations': 1, 'replaces': 1, 'deferred': 0, 'case_id': 7} | PASS |
| M0首次完成及获取释放 / YUV422 | 完成M0 | 替换M0为Buffer2，Buffer0交付，重复Get/Release被拒绝 | {'fail_line': 0, 'checks': 49, 'operations': 1, 'replaces': 1, 'deferred': 0, 'case_id': 8} | PASS |
| M1首次完成及获取释放 / Y8 | 完成M1 | 替换M1为Buffer2，Buffer1交付，重复Get/Release被拒绝 | {'fail_line': 0, 'checks': 49, 'operations': 1, 'replaces': 1, 'deferred': 0, 'case_id': 9} | PASS |
| M1首次完成及获取释放 / RGB565 | 完成M1 | 替换M1为Buffer2，Buffer1交付，重复Get/Release被拒绝 | {'fail_line': 0, 'checks': 49, 'operations': 1, 'replaces': 1, 'deferred': 0, 'case_id': 10} | PASS |
| M1首次完成及获取释放 / YUV422 | 完成M1 | 替换M1为Buffer2，Buffer1交付，重复Get/Release被拒绝 | {'fail_line': 0, 'checks': 49, 'operations': 1, 'replaces': 1, 'deferred': 0, 'case_id': 11} | PASS |
| 旧READY持续覆盖 / Y8 | 交替完成30帧，不获取 | 保留最新帧30，丢帧29，映射与硬件地址一致 | {'fail_line': 0, 'checks': 19824, 'operations': 30, 'replaces': 30, 'deferred': 0, 'case_id': 12} | PASS |
| 旧READY持续覆盖 / RGB565 | 交替完成30帧，不获取 | 保留最新帧30，丢帧29，映射与硬件地址一致 | {'fail_line': 0, 'checks': 39024, 'operations': 30, 'replaces': 30, 'deferred': 0, 'case_id': 13} | PASS |
| 旧READY持续覆盖 / YUV422 | 交替完成30帧，不获取 | 保留最新帧30，丢帧29，映射与硬件地址一致 | {'fail_line': 0, 'checks': 39024, 'operations': 30, 'replaces': 30, 'deferred': 0, 'case_id': 14} | PASS |
| Application长时间持有 / Y8 | 获取第一帧后1000次DMA完成 | 持有帧字节不变，不切换IN_USE，丢帧1000，释放后可重新交付 | {'fail_line': 0, 'checks': 40230, 'operations': 1002, 'replaces': 2, 'deferred': 0, 'case_id': 15} | PASS |
| Application长时间持有 / RGB565 | 获取第一帧后1000次DMA完成 | 持有帧字节不变，不切换IN_USE，丢帧1000，释放后可重新交付 | {'fail_line': 0, 'checks': 59430, 'operations': 1002, 'replaces': 2, 'deferred': 0, 'case_id': 16} | PASS |
| Application长时间持有 / YUV422 | 获取第一帧后1000次DMA完成 | 持有帧字节不变，不切换IN_USE，丢帧1000，释放后可重新交付 | {'fail_line': 0, 'checks': 59430, 'operations': 1002, 'replaces': 2, 'deferred': 0, 'case_id': 17} | PASS |
| FREE目标切换失败 / Y8 | 切换回调依次返回IO/BUSY/TIMEOUT | 原错误返回，整个软件对象及硬件目标不变，成功重试可提交 | {'fail_line': 0, 'checks': 131, 'operations': 6, 'replaces': 6, 'deferred': 0, 'case_id': 18} | PASS |
| FREE目标切换失败 / RGB565 | 切换回调依次返回IO/BUSY/TIMEOUT | 原错误返回，整个软件对象及硬件目标不变，成功重试可提交 | {'fail_line': 0, 'checks': 131, 'operations': 6, 'replaces': 6, 'deferred': 0, 'case_id': 19} | PASS |
| FREE目标切换失败 / YUV422 | 切换回调依次返回IO/BUSY/TIMEOUT | 原错误返回，整个软件对象及硬件目标不变，成功重试可提交 | {'fail_line': 0, 'checks': 131, 'operations': 6, 'replaces': 6, 'deferred': 0, 'case_id': 20} | PASS |
| READY目标切换失败 / Y8 | 有旧READY时切换失败 | 旧帧仍保留，计数和软件对象不变，成功重试才回收 | {'fail_line': 0, 'checks': 57734, 'operations': 9, 'replaces': 9, 'deferred': 0, 'case_id': 21} | PASS |
| READY目标切换失败 / RGB565 | 有旧READY时切换失败 | 旧帧仍保留，计数和软件对象不变，成功重试才回收 | {'fail_line': 0, 'checks': 115334, 'operations': 9, 'replaces': 9, 'deferred': 0, 'case_id': 22} | PASS |
| READY目标切换失败 / YUV422 | 有旧READY时切换失败 | 旧帧仍保留，计数和软件对象不变，成功重试才回收 | {'fail_line': 0, 'checks': 115334, 'operations': 9, 'replaces': 9, 'deferred': 0, 'case_id': 23} | PASS |
| 初始化非法参数 / Y8 | NULL、奇数宽、未知格式 | 返回INVALID_ARG，已有对象不变 | {'fail_line': 0, 'checks': 9, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 24} | PASS |
| 初始化非法参数 / RGB565 | NULL、奇数宽、未知格式 | 返回INVALID_ARG，已有对象不变 | {'fail_line': 0, 'checks': 9, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 25} | PASS |
| 初始化非法参数 / YUV422 | NULL、奇数宽、未知格式 | 返回INVALID_ARG，已有对象不变 | {'fail_line': 0, 'checks': 9, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 26} | PASS |
| 完成回调非法参数 / Y8 | NULL对象/回调、非法槽位 | 返回INVALID_ARG，无副作用 | {'fail_line': 0, 'checks': 7, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 27} | PASS |
| 完成回调非法参数 / RGB565 | NULL对象/回调、非法槽位 | 返回INVALID_ARG，无副作用 | {'fail_line': 0, 'checks': 7, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 28} | PASS |
| 完成回调非法参数 / YUV422 | NULL对象/回调、非法槽位 | 返回INVALID_ARG，无副作用 | {'fail_line': 0, 'checks': 7, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 29} | PASS |
| DMA索引越界 / Y8 | M0索引3 | 返回INVALID_STATE，无地址切换 | {'fail_line': 0, 'checks': 5, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 30} | PASS |
| DMA索引越界 / RGB565 | M0索引3 | 返回INVALID_STATE，无地址切换 | {'fail_line': 0, 'checks': 5, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 31} | PASS |
| DMA索引越界 / YUV422 | M0索引3 | 返回INVALID_STATE，无地址切换 | {'fail_line': 0, 'checks': 5, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 32} | PASS |
| DMA目标重叠 / Y8 | M0/M1同一缓冲区 | 返回INVALID_STATE，无地址切换 | {'fail_line': 0, 'checks': 5, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 33} | PASS |
| DMA目标重叠 / RGB565 | M0/M1同一缓冲区 | 返回INVALID_STATE，无地址切换 | {'fail_line': 0, 'checks': 5, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 34} | PASS |
| DMA目标重叠 / YUV422 | M0/M1同一缓冲区 | 返回INVALID_STATE，无地址切换 | {'fail_line': 0, 'checks': 5, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 35} | PASS |
| DMA所有权错误 / Y8 | 目标为READY | 返回INVALID_STATE，无地址切换 | {'fail_line': 0, 'checks': 5, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 36} | PASS |
| DMA所有权错误 / RGB565 | 目标为READY | 返回INVALID_STATE，无地址切换 | {'fail_line': 0, 'checks': 5, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 37} | PASS |
| DMA所有权错误 / YUV422 | 目标为READY | 返回INVALID_STATE，无地址切换 | {'fail_line': 0, 'checks': 5, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 38} | PASS |
| 消费者索引越界 / Y8 | consumer索引3 | 返回INVALID_STATE，无地址切换 | {'fail_line': 0, 'checks': 5, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 39} | PASS |
| 消费者索引越界 / RGB565 | consumer索引3 | 返回INVALID_STATE，无地址切换 | {'fail_line': 0, 'checks': 5, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 40} | PASS |
| 消费者索引越界 / YUV422 | consumer索引3 | 返回INVALID_STATE，无地址切换 | {'fail_line': 0, 'checks': 5, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 41} | PASS |
| 消费者与DMA目标冲突 / Y8 | consumer等于M0 | 返回INVALID_STATE，无地址切换 | {'fail_line': 0, 'checks': 5, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 42} | PASS |
| 消费者与DMA目标冲突 / RGB565 | consumer等于M0 | 返回INVALID_STATE，无地址切换 | {'fail_line': 0, 'checks': 5, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 43} | PASS |
| 消费者与DMA目标冲突 / YUV422 | consumer等于M0 | 返回INVALID_STATE，无地址切换 | {'fail_line': 0, 'checks': 5, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 44} | PASS |
| 消费者指向FREE / Y8 | 第三块FREE且consumer关联 | 返回INVALID_STATE，无地址切换 | {'fail_line': 0, 'checks': 5, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 45} | PASS |
| 消费者指向FREE / RGB565 | 第三块FREE且consumer关联 | 返回INVALID_STATE，无地址切换 | {'fail_line': 0, 'checks': 5, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 46} | PASS |
| 消费者指向FREE / YUV422 | 第三块FREE且consumer关联 | 返回INVALID_STATE，无地址切换 | {'fail_line': 0, 'checks': 5, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 47} | PASS |
| READY缺少消费者关联 / Y8 | 第三块READY、consumer无效 | 返回INVALID_STATE，无地址切换 | {'fail_line': 0, 'checks': 5, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 48} | PASS |
| READY缺少消费者关联 / RGB565 | 第三块READY、consumer无效 | 返回INVALID_STATE，无地址切换 | {'fail_line': 0, 'checks': 5, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 49} | PASS |
| READY缺少消费者关联 / YUV422 | 第三块READY、consumer无效 | 返回INVALID_STATE，无地址切换 | {'fail_line': 0, 'checks': 5, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 50} | PASS |
| 替换地址容量保护 / Y8 | NULL地址、零长度、超容量、非Word长度 | 返回INVALID_STATE，软件对象不变，无地址切换 | {'fail_line': 0, 'checks': 22, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 51} | PASS |
| 替换地址容量保护 / RGB565 | NULL地址、零长度、超容量、非Word长度 | 返回INVALID_STATE，软件对象不变，无地址切换 | {'fail_line': 0, 'checks': 22, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 52} | PASS |
| 替换地址容量保护 / YUV422 | NULL地址、零长度、超容量、非Word长度 | 返回INVALID_STATE，软件对象不变，无地址切换 | {'fail_line': 0, 'checks': 22, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 53} | PASS |
| 获取释放NULL参数 / Y8 | NULL对象/配置/输出 | Get为false、Release为INVALID_ARG，输出不变 | {'fail_line': 0, 'checks': 7, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 54} | PASS |
| 获取释放NULL参数 / RGB565 | NULL对象/配置/输出 | Get为false、Release为INVALID_ARG，输出不变 | {'fail_line': 0, 'checks': 7, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 55} | PASS |
| 获取释放NULL参数 / YUV422 | NULL对象/配置/输出 | Get为false、Release为INVALID_ARG，输出不变 | {'fail_line': 0, 'checks': 7, 'operations': 0, 'replaces': 0, 'deferred': 0, 'case_id': 56} | PASS |
| PRIMASK原值0恢复 / Y8 | Get/Release成功与失败 | 出口恢复0 | {'fail_line': 0, 'checks': 11, 'operations': 1, 'replaces': 1, 'deferred': 0, 'case_id': 57} | PASS |
| PRIMASK原值0恢复 / RGB565 | Get/Release成功与失败 | 出口恢复0 | {'fail_line': 0, 'checks': 11, 'operations': 1, 'replaces': 1, 'deferred': 0, 'case_id': 58} | PASS |
| PRIMASK原值0恢复 / YUV422 | Get/Release成功与失败 | 出口恢复0 | {'fail_line': 0, 'checks': 11, 'operations': 1, 'replaces': 1, 'deferred': 0, 'case_id': 59} | PASS |
| PRIMASK原值1恢复 / Y8 | Get/Release成功与失败 | 出口恢复1 | {'fail_line': 0, 'checks': 11, 'operations': 1, 'replaces': 1, 'deferred': 0, 'case_id': 60} | PASS |
| PRIMASK原值1恢复 / RGB565 | Get/Release成功与失败 | 出口恢复1 | {'fail_line': 0, 'checks': 11, 'operations': 1, 'replaces': 1, 'deferred': 0, 'case_id': 61} | PASS |
| PRIMASK原值1恢复 / YUV422 | Get/Release成功与失败 | 出口恢复1 | {'fail_line': 0, 'checks': 11, 'operations': 1, 'replaces': 1, 'deferred': 0, 'case_id': 62} | PASS |
| Get临界区完成中断交错 / Y8 | 屏蔽后排队M1完成 | 退出临界区后才执行，已获取帧不被覆盖 | {'fail_line': 0, 'checks': 19225, 'operations': 2, 'replaces': 1, 'deferred': 1, 'case_id': 63} | PASS |
| Get临界区完成中断交错 / RGB565 | 屏蔽后排队M1完成 | 退出临界区后才执行，已获取帧不被覆盖 | {'fail_line': 0, 'checks': 38425, 'operations': 2, 'replaces': 1, 'deferred': 1, 'case_id': 64} | PASS |
| Get临界区完成中断交错 / YUV422 | 屏蔽后排队M1完成 | 退出临界区后才执行，已获取帧不被覆盖 | {'fail_line': 0, 'checks': 38425, 'operations': 2, 'replaces': 1, 'deferred': 1, 'case_id': 65} | PASS |
| Release临界区完成中断交错 / Y8 | 屏蔽后排队M1完成 | 先释放再处理完成，重新产生READY | {'fail_line': 0, 'checks': 25, 'operations': 2, 'replaces': 2, 'deferred': 1, 'case_id': 66} | PASS |
| Release临界区完成中断交错 / RGB565 | 屏蔽后排队M1完成 | 先释放再处理完成，重新产生READY | {'fail_line': 0, 'checks': 25, 'operations': 2, 'replaces': 2, 'deferred': 1, 'case_id': 67} | PASS |
| Release临界区完成中断交错 / YUV422 | 屏蔽后排队M1完成 | 先释放再处理完成，重新产生READY | {'fail_line': 0, 'checks': 25, 'operations': 2, 'replaces': 2, 'deferred': 1, 'case_id': 68} | PASS |
| 原先屏蔽时中断持续排队 / Y8 | 初始PRIMASK=1，Get期间完成 | Get后仍不执行，显式恢复0才处理完成 | {'fail_line': 0, 'checks': 27, 'operations': 2, 'replaces': 1, 'deferred': 1, 'case_id': 69} | PASS |
| 原先屏蔽时中断持续排队 / RGB565 | 初始PRIMASK=1，Get期间完成 | Get后仍不执行，显式恢复0才处理完成 | {'fail_line': 0, 'checks': 27, 'operations': 2, 'replaces': 1, 'deferred': 1, 'case_id': 70} | PASS |
| 原先屏蔽时中断持续排队 / YUV422 | 初始PRIMASK=1，Get期间完成 | Get后仍不执行，显式恢复0才处理完成 | {'fail_line': 0, 'checks': 27, 'operations': 2, 'replaces': 1, 'deferred': 1, 'case_id': 71} | PASS |
| 独立模型随机状态机 / Y8 | 固定种子3000步：完成/获取/释放/切换失败 | 每步与独立模型一致，DMA始终拥有两块且不覆盖IN_USE | {'fail_line': 0, 'checks': 97288, 'operations': 1845, 'replaces': 1039, 'deferred': 0, 'case_id': 72} | PASS |
| 独立模型随机状态机 / RGB565 | 固定种子3000步：完成/获取/释放/切换失败 | 每步与独立模型一致，DMA始终拥有两块且不覆盖IN_USE | {'fail_line': 0, 'checks': 97288, 'operations': 1845, 'replaces': 1039, 'deferred': 0, 'case_id': 73} | PASS |
| 独立模型随机状态机 / YUV422 | 固定种子3000步：完成/获取/释放/切换失败 | 每步与独立模型一致，DMA始终拥有两块且不覆盖IN_USE | {'fail_line': 0, 'checks': 97288, 'operations': 1845, 'replaces': 1039, 'deferred': 0, 'case_id': 74} | PASS |

{'total': 75, 'passed': 75, 'failed': 0, 'assertions': 934605}

正式源文件变化：[]

限制：不验证真实DMA双缓冲CT/中断时序、BSP切换实现、传感器信号、MPU或像素格式正确性。模拟DMA写入仅为测试数据。
