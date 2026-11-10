# Stage 08F：生产合并边界

日期：2026-11-10。开发与历史排查保留在08D/08F分支，主线候选从原main单独建立。

## 进入主线的内容

- SDMMC1 IDMA和块设备、FatFS适配及正式文件接口。
- JPEG双路借用、七帧管理、Storage任务、录像和抓拍公共接口。
- 受控8KiB文件输入中转、原AXI IDMA中转及错误恢复。
- Keil Application/Service分组整理，保持CM4排除设置。
- 必要运行状态和错误记录，JPEG完整性、原有CRC/回读校验及帧所有权保护。

## 不进入主线的内容

- 本轮新增或修改的Tests目录、运行脚本、Mock、结果JSON和日志。
- 本轮Docs/Debug记录、HEX导出、output和tmp。
- g_storage_timing_debug、g_storage_io_timing、g_sd_write_debug。
- g_sd_copy_debug及DCMI到SD复制探针的跨模块调用。
- g_storage_debug.request的Watch手动触发入口和复制/写入测时字段。
- 用户本机uvoptx的断点、Watch以及旧工程专属配置变更。

原main已有测试和文档保持原样，不因本次排除而整目录删除。为避免把测试提交历史带入main，主线候选只取生产目录的最终文件差异，不直接合并带测试的08F历史。

## 正式调用

存储默认不自动保存。业务调用App_StorageStartRecording、App_StorageRequestSnapshot、App_StorageStop或App_StorageRecover提交请求，StorageTask负责执行；不再通过Watch写request。

正式状态为g_storage_status，外部优先使用App_StorageGetStatus获取快照。该状态不作为Media/USB所有权释放证明。

当前应用存储配置partition=3，对应用户已建立的第三个MBR FAT32卷；不格式化、不修改分区。0表示沿用自动选择首个FAT分区，其他卡需明确核对后配置。分区选择属于Application，底层不硬编码3。

## 验证

生产清理版用真实源码执行：帧租借/录像46个功能场景×O0/O2，92/92；FatFS原60个功能场景×O0/O2，120/120；BSP/HAL块设备556/556。移除探针后其专属测试单列为不适用，不冒充通过。

取消借帧测试的注入点从已删除的测时调用移到测试桩恢复中断的位置，原取消及资源断言保留；生产不添加测试钩子。验证适配器仅在开发分支保留，不进入main。

CM7/CM4需在候选最终目录完整重编，并审计链接无临时探针符号。历史不兼容测试不宣称全仓全绿。

## 未完成项

录像25FPS尚未完成，FIFO故障与FatFS读取的硬件因果机制未定。本轮去除观测代码后不宣称新增实板验收或问题根治。实际卡读写、长期运行、停止恢复仍需与最终固件复验。
