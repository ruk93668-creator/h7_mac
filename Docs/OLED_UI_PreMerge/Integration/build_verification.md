# Keil 正式完整构建

源码提交 `72140cd55ebc82a8ce4d6e5d5c8a6c5f9a143c6a`。ARMCC 5.06 update 7。以下六组均为 0 Error / 0 Warning。CM7 JPEG 与 Raw 分别生成 Intel HEX 并逐字节与对应的 main 产物一致。Keil 双 Target 对称登记 OLED 路径，CM7 排除 CM4 UI 及 U7 BSP。

| 目标 | Code / RO / RW / ZI (B) | Flash (B) | 静态 RAM (B) | Intel HEX SHA256 |
| --- | --- | ---: | ---: | --- |
| main-cm4 | 26302 / 1438 / 936 / 3576 | 28,676 | 4,512 | `3030484c03d4983a192be39142dce7b6dfdb8e6c93a49e1f8285ee846540fb17` |
| candidate-paired-cm4 | 39978 / 19990 / 976 / 3776 | 60,944 | 4,752 | `e3495a80b774867de9317195fc5448fbfc2757a10494b207a0960a7ebab53fc3` |
| main-jpeg | 140338 / 3922 / 1296 / 100960 | 145,556 | 102,256 | `c981f1ebd61e14864e4a1a433d031363f7a767f427a24c5aca635d9c58fa686b` |
| candidate-final-jpeg | 140338 / 3922 / 1296 / 100960 | 145,556 | 102,256 | `c981f1ebd61e14864e4a1a433d031363f7a767f427a24c5aca635d9c58fa686b` |
| main-raw | 140338 / 3922 / 1296 / 100960 | 145,556 | 102,256 | `552fe5c5b76f71ad297071a8ff110a2c2475331cfa221a927fb39fe252ed3581` |
| candidate-final-raw | 140338 / 3922 / 1296 / 100960 | 145,556 | 102,256 | `552fe5c5b76f71ad297071a8ff110a2c2475331cfa221a927fb39fe252ed3581` |

CM4 Flash 增量 +32,268 B，静态 RAM 增量 +240 B。启动脚本保留 1024 B 栈，实际栈峰值待实测。Device Framebuffer 512 B，应用回滚副本 512 B。正式 Map 阶段/测试标志扫描为零。

```text
s_oled                                   0x10000064   Data         544  oled_device.o(.data)
s_page_state                             0x100002ac   Data          40  app_oled.o(.bss)
s_oled_input_queue                       0x100002d4   Data          16  app_oled.o(.bss)
s_oled_anim                              0x100002e4   Data          16  app_oled.o(.bss)
s_oled_popup                             0x100002f4   Data          44  app_oled.o(.bss)
s_oled_tools                             0x10000320   Data          64  app_oled.o(.bss)
s_frame_backup                           0x10000370   Data         512  app_oled.o(.bss)
Stack_Mem                                0x10000d50   Data        1024  startup_stm32h747xx_cm4.o(STACK)
__initial_sp                             0x10001150   Data           0  startup_stm32h747xx_cm4.o(STACK)
```

完整 Map 和 HEX 位于本地 `IntegrationEvidence`，构建日志位于 [Evidence](Evidence/)。原失败日志亦保存在本地。
