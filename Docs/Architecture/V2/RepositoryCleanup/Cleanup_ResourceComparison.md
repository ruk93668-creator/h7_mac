# Cleanup 资源对比

基线和候选使用清理前后的实际用户工程配置。单位为字节。Flash 预算为 Code + RO + RW。

| Target | 配置 | Code | RO | RW | ZI | Flash | DTCM 或 CM4 RW_IRAM1 | AXI SRAM SDMMC DMA | 共享区 |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| CM7 | 清理前 | 179826 | 5694 | 1828 | 107676 | 187348 | 76416 | 32768 | 320 |
| CM7 | 清理后 | 179314 | 5666 | 1828 | 106812 | 186808 | 75552 | 32768 | 320 |
| CM4 | 清理前 | 26350 | 1438 | 936 | 3576 | 28724 | 4192 | 0 | 320 |
| CM4 | 清理后 | 26350 | 1438 | 936 | 3576 | 28724 | 4192 | 0 | 320 |

Legacy Camera 上下文 988 → 132 B。CM7 Flash 预算变化 -540 B，ZI 变化 -864 B，DTCM 变化 -864 B。
清理前静态栈 {'s_idle_stack': 1024, 's_camera_stack': 6144, 's_usb_stack': 8192, 's_control_stack': 3072, 's_ipc_stack': 2048, 's_stack': 8192}。
清理后静态栈 {'s_idle_stack': 1024, 's_camera_stack': 6144, 's_usb_stack': 8192, 's_control_stack': 3072, 's_ipc_stack': 2048, 's_stack': 8192}。
SDRAM 未出现在两版链接执行区。Camera/Media 外部资源定义未改，真实运行期容量与任务栈高水位未测。

| 构建 / 文件 | SHA256 |
|---|---|
| bu / CM7 AXF | 8eeb233d5022650def849662ed6992675bf03aaf9928c6ed85e5f4c08574b90e |
| bu / CM7 MAP | 535bfc13bf5b7f38ef21f1c97409aff21eb60cc183197387351381cf540d59d1 |
| bu / CM7 HEX | 76db3f278bbdf4c0602d0867b62ef1c214c2ba2900d963ab0bfe4be97daa25a1 |
| bu / CM4 AXF | 0b0c64c7aff49a9dce1de7c73ab2e56e57bfb8b46ddccc2e42d01178fa93b7c7 |
| bu / CM4 MAP | 725331662b52028e4c67a49af930289c53d2bff65440e6d9e29a009d44b51b7e |
| bu / CM4 HEX | 7be9685cb70201486281dcc0a327ea485705f3c7ba271a5dc2e1feb7ccdbd301 |
| c / CM7 AXF | e9a420c8d91bb3c743596048ba513e77faebf4ed7d02fcc8f7e32a144b272fed |
| c / CM7 MAP | 8dcb8747064f9018b99005e63a267d302b8235d9e4af412bae03ade88c6e58d0 |
| c / CM7 HEX | 7a1aa139c5959e92733df3375c8fbb8ca5aa40f879fcec4461430e73dda90271 |
| c / CM4 AXF | 2c745bdeadeda760149da36cbdb90a2638b4ebcb0f7130a64019fc896da00f24 |
| c / CM4 MAP | 32089d1e75c6705b97fd28233388b32a483edf03119e74bbc51261b802a32651 |
| c / CM4 HEX | 7be9685cb70201486281dcc0a327ea485705f3c7ba271a5dc2e1feb7ccdbd301 |

构建日志、AXF、Map、HEX 和哈希清单位于 output/q124。
