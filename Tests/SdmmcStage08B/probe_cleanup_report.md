# SD验收探针从正式固件移除

日期：2026-11-08。分支codex/stage08b-sdmmc-idma，不合并main。本轮只清理，下一阶段开发由用户另行安排。

## 清理内容

- 正式Keil工程移除sdmmc_probe.c和sd_block_probe.c；CM4 Target里原本排除构建的同名展示项也一并清理，不修改CM4源码和行为。
- 删除项目中的APP_SDMMC_*、APP_SD_BLOCK_*和SD_TEST_*宏，移除实卡CID、容量及原始LBA写测试授权配置。不是仅把开关改为0。
- 删除RW_SD_STRESS_CPU及其约33KiB压力缓冲链接区域。没有删除SDMMC IDMA所需的32KiB区。
- 正式固件不再链接SdTest任务、g_sd_block_test、g_sdmmc_test及压力缓冲，也不再注册这两个测试模块的Initcall。旧Watch request不再是当前固件接口。
- 独立Tests源码、读写恢复测试、压力测试和历史验收报告保留，不在正式固件中运行。output/、tmp/与用户原有修改保留。

## 保留内容

bsp_sdmmc.c/.h、bsp_sdmmc_memory.c、sd_block.c/.h及HAL SD/LL SDMMC保持原样。保留400kHz/1-bit识卡后切换4-bit/40MHz的能力、PH14内部下拉且高有效的插卡检测、SDMMC1 IRQ、32KiB AXI Non-cacheable IDMA区域、MPU和错误恢复机制。

SdBlock_Prepare仍只准备静态同步资源；SDMMC内存准备也保留。正常固件没有自动识卡/读写业务入口，后续需由正式调用方在允许的任务上下文调用SD_Block_Init/Read/Write。移除测试不等于移除驱动，也不把初始化后的ready值写死成成功。

Camera、USB、Control、IPC的1000ms栈余量扫描规则保留，CM7/CM4/Common业务源码相对清理前没有任何修改。SD测试中的扫描仍保留在独立验收源文件中，但该文件不再链接进固件。

## 检查结果

- CM7/CM4现有Target增量构建均0 Error、0 Warning；记录见normal_cleanup_builds.json与两份normal_cleanup日志。不是额外全量重编。
- `python Tests/SdmmcStage08B/check_normal_firmware.py`：实际工程和AXF检查15/15通过。确认无测试宏/入口/符号，SD底层源文件仍参与构建，IDMA缓冲仍在0x24000000且大小32768，Platform同步准备仍链接，无USB耗时采样器。
- 本轮仅移除工程接入和测试专用Scatter区，未改BSP/Platform的C源码或历史测试断言；没有重新运行全仓回归，也没有进行新一轮实卡读写。
- 用户此前已反馈识卡、读写回读与原数据恢复、5分钟只读压力通过；之后反馈栈扫描降频后USB恢复25 FPS。没有据此补造“降频后再次同时压力5分钟并保持25FPS”的测试记录。
- 本清理固件尚待用户烧录确认Camera/USB/IPC/OLED运行正常，不把成功构建等同于新镜像实板验收。

## 使用与后续

重新打开原MDK-ARM/h7_mac.uvprojx，避免已打开的Keil窗口用旧项目内容覆盖清理结果。无需再设置g_sd_block_test.request，也不需要额外测试工程。独立测试若将来需要复用，应按专项流程显式构建，不恢复自动测试Initcall到正常固件。

本轮不接FatFS、MSC或JPEG文件保存，不新建StorageTask，不执行任何实卡写入。后续工作等待用户安排。
