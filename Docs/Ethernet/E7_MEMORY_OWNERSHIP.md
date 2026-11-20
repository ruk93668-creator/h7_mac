# E7 内存与所有权

SDRAM Region 2 与生产属性一致：`0xD0000000..0xD0800000`，8 MiB，Normal 非缓存、shareable、XN。E7 独立入口在 DCache 开启前配置 Region 2、4、5。表中区间为左闭右开。

| 区间 | 所有者 |
| --- | --- |
| `0xD0000000..0xD0000400` | 原 SDRAM 保留区 |
| `0xD0000400..0xD041A400` | Snapshot 与七槽 YUY2 原保留范围，E7 不启动这些模式 |
| `0xD0480000..0xD0500000` | 512 KiB 连续 JPEG FIFO，64 个 8 KiB 块 |
| `0xD0500000..0xD0504000` | 两个 DCMI drop 块 |
| `0xD0590000..0xD05A0000` | Storage 的 AVI 索引保留范围，E7 不启动 Storage |
| `0xD07F0000..0xD07FF000` | 60 KiB E7 Network 私有缓冲，32 B 对齐，CPU 写读 |
| `0x24078000..0x24080000` | Region 4 非缓存 Ethernet DMA 窗口 |
| `0x24010000..0x24020000` | Region 5 缓存 CPU 窗口，含 E5 TCP 环和 lwIP |

私有缓冲在专用 UNINIT 区，目标节为 Zero RW，无 Flash 装载内容，FMC 初始化前无需写 SDRAM。构建验证 MAP 地址、尺寸、Zero RW 和 UNINIT。单缓冲配合最新 READY，慢读时限制待传帧数量。

流程：DMA 写 FIFO → Media READY → E7 短期借用 → 按总长减第二段长度复制第一段 → 回绕时接第二段 → 检查 SOI/EOI 并计算 CRC32 → 归还原借用 → 私有缓冲租约 → E5 TCP 复制环 → lwIP → Ethernet DMA 私有缓冲。两个源区间都校验，超出 60 KiB 在转换到 16 位前拒绝。复制或验证失败同样尝试归还原借用。归还失败时私有帧不发布、原借用状态冻结。私有租约在最后一块 JPEG 返回 ERR_OK 或当前发送终止时归还，与 TCP ACK 无关。重复或旧租约归还不会解锁新帧。
