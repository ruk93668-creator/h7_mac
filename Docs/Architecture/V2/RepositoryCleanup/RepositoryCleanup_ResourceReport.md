# Repository Cleanup 资源对照

同一用户 Keil Target 和 ARMCC5 设置，单位 B。基线是 `4a5db4e`，候选实施提交为 `519de52`。

| Target | 项 | 基线 | 候选 | 差量 |
|---|---|---:|---:|---:|
| CM7 | Code | 179962 | 179830 | -132 |
| CM7 | RO-data | 5734 | 5694 | -40 |
| CM7 | RW-data | 1828 | 1828 | 0 |
| CM7 | ZI-data | 108580 | 107676 | -904 |
| CM4 | Code | 26350 | 26350 | 0 |
| CM4 | RO-data | 1438 | 1438 | 0 |
| CM4 | RW-data | 936 | 936 | 0 |
| CM4 | ZI-data | 3576 | 3576 | 0 |

CM7 DTCM `RW_IRAM1` 从 0x12E08 减到 0x12A80（-904 B），Initcall 区从 0xC0 减到 0xB4。AXI SRAM 的 SDMMC DMA 区仍为 0x8000，SRAM4 共享区仍为 0x140。没有 SDRAM 链接区变化，既有 Frame Pool 与外部 SDRAM 配置不在改动内。任务栈配置保持原样，实板栈余量与 WCET 为 NOT_RUN。

## 共享代次声明续审

对照 `db02062` 与候选的 CM7 Code/RO/RW/ZI 均为 179826/5694/1828/107676 B，CM4 均为 26350/1438/936/3576 B，本轮增量全部为 0。相较前一表中的 `519de52`，`db02062` 之前的排版整理包含 Composition 等价守卫调整，CM7 Code 减少 4 B。实板栈水位仍为 NOT_RUN。
