# 静态资源及内存预算

审查日期：2026-11-26（Asia/Shanghai）。源码冻结：`78580073610ff4cfe06a4b004b1b0d75bffef499`；main：`851c1115fe097f7fd2e0bdcaa5f7b3f8c03feb71`。

| 区域 | Base | 已用 / 上限 | 剩余 |
|---|---|---|---|
| RW_ETHERNET_DMA | 0x24078000 | 12544 / 32768 | 20224 |
| RW_ETHERNET_CPU | 0x24010000 | 43232 / 65536 | 22304 |
| RW_NETWORK_JPEG | 0xD07F0000 | 61440 / 61440 | 0 |
| RW_IRAM1 | 0x20000000 | 83136 / 131072 | 47936 |

新增Network栈8192字节；旧Camera1536字、USB2048字、Storage2048字、Control768字和IPC资源不变。
消息槽9→12，每槽4×52字节，消息数据净增624字节；Record信号3→4增4字节；TCB/QueueSlot等见MAP。
lwIP沿用固定12KiB heap、24 PBUF_POOL、6 TCP PCB、1 listen PCB、24 TCP SEG、2 UDP PCB及内部timer+2余量，
不使用动态任务或无界应用分配绕过静态预算。

私有JPEG61440字节在0xD07F0000，UNINIT；Camera FIFO0xD0480000起512KiB及16KiB丢弃区，
录像索引0xD0590000起1MiB，私有区不重叠。
SDRAM Region2不变，DMA Region4大小/属性不变，CPU Region5沿用64KiB缓存设计。
Framework准备SDMMC Region3后才启用DCache，USB FS DMA仍关闭，Camera/IPC用原非缓存窗口。

参考Q10整合基线Code203434/RO5702/RW1592/ZI156744（历史参考，不冒充本轮main重编译）。
最终目标精确Code/RO/RW/ZI在build-results.json；ZI增加含61440字节外部SDRAM，不能当作DTCM占用。
未扩大DTCM/DMA窗口。静态预算不是运行时水位证明；现有INCLUDE_uxTaskGetStackHighWaterMark=1，
N5须保存真实最小剩余栈，本轮没有栈水位实测值。
