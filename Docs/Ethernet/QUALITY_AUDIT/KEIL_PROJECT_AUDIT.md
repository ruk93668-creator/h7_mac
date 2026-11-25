# Q7 Keil 工程

结论 **PASS**（链接与当前默认启动范围），实板属性 **NOT_VERIFIED**。详见 project-index.md 及 BUILD_REGRESSION.md。正式 CM7 新增 Ethernet lwIP port、Service、lwIP 三组，沿用正式入口和 FreeRTOS 项。DHCP/E5 宏启用协议编译，NO_SYS=1，链接器保留入口只用于验证。CM4 目标配置对比、重复编译源和不存在的源路径见索引。RTE 工程名注释保留。

| 链接区 | 基址 | 容量 |
| --- | --- | --- |
| CM7 Flash | 0x08000000 | 1 MiB |
| DTCM | 0x20000000 | 128 KiB |
| IPC | 0x38000000 | 4 KiB |
| SDMMC DMA | 0x24000000 | 32 KiB |
| Ethernet CPU | 0x24010000 | 64 KiB |
| Ethernet DMA / MPU 4 | 0x24078000 | 32 KiB |
| 外置 SDRAM / MPU 2 | 0xD0000000 | 8 MiB |

CPU 预留窗口与 SDMMC/Ethernet DMA 无重叠。正式无独立 JPEG 私有缓冲区。MPU 4 非缓存用于 Descriptor/Buffer，正式启动未建立独立测试的 MPU 5 配置。实际 Cache、MPU、IRQ 与并行实板证据仍缺，见 ETH-Q07。
