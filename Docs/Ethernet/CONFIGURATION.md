# E2/E3 公共配置影响

本次公共配置单独提交，Ethernet 不注册 Framework、不创建任务、不接入 Application。

| 文件 | 改动 | 影响 |
| --- | --- | --- |
| CM7/Core/Src/main.c | 在原 MPU 禁用期间配置 Ethernet region 4 | 早于 DMA 内存访问；不调用 MAC 初始化或启动 |
| MDK-ARM/h7_mac_CM7_custom.sct | 增加 RW_ETHERNET_DMA | 独占 AXI SRAM 最后 32 KiB |
| MDK-ARM/h7_mac.uvprojx | 仅 CM7 增加 include、四个源文件和 section keep | CM4 target 配置保持原样；显式保留 DMA 内存 |
| MDK-ARM/.gitignore | 忽略隔离验证工程 | 防止提交本地生成项目 |
| .gitignore | 忽略 output/Ethernet | 构建映像、map、原理图渲染及本地参考资料不提交 |

没有修改 ETH HAL 开关；后端直接使用本仓库 H747 CMSIS 寄存器定义，GPIO、RCC 和 NVIC 使用已有 HAL。不得混用 HAL_ETH 的数据路径或另一个 ETH_IRQHandler。

region 0 为既有默认保护，1 为 IPC，2 为 SDRAM，3 为 SDMMC，4 分配给 Ethernet。新区域为 Normal、shareable、noncacheable、nonbufferable、XN、full access，地址 0x24078000，大小 32 KiB。当前 MPU 配置在复位启动且 DCache 尚未开启时执行。运行中不能改变该区域属性或映射缓存别名。

CM4 默认 RW 区 0x10000000–0x10047FFF 是 D2 SRAM 的别名，覆盖 SRAM1/2/3。因此本次不占用 D2，也不修改 CM4 linker、OLED 或启动代码。既有 SDMMC 区仍为 0x24000000–0x24007FFF。IPC 仍为 0x38000000 的原区域。

AXI 窗口共预留 32768 字节，实际 Ethernet 内存 12544 字节，余下 20224 字节不得供其他模块使用。链接器限制 section 容量；运行时检查实际符号地址、MPU 启用、region 4 完整属性及高优先级区域覆盖。

Keil 验证脚本只生成 MDK-ARM/h7_mac_ethernet_verify.uvprojx，并在 output/Ethernet 下生成映像。脚本保存/恢复 Keil 自动改写的 RTE 注释；不下载固件。基线使用 b282c7c 的 main、工程和 scatter，不引用改后的 scatter。请串行运行这些构建，避免 Keil 共用生成工程发生竞争。

FreeRTOS 任务、栈、优先级、队列、时钟树、现有 NVIC priority 均未修改。Ethernet IRQ 只 enable/disable，保留其 priority；ISR 不调用任何 RTOS API。
