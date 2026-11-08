# FatFS Stage08C设计

## 调用关系

Application → Platform/storage_fs → 官方FatFS → Platform/diskio → Platform/sd_block → BSP/bsp_sdmmc → SDMMC1 IDMA → SD卡。

普通C文件句柄StorageFile_t由槽位及代号构成，不向应用暴露FIL。最多4个文件。Initcall仅准备静态状态，不能识卡、挂载或自动写入；正常固件没有文件系统业务任务。需要文件操作的任务显式Mount，随后由同一任务Open/Read/Write/Sync/Close/Unmount。

## 所有权与错误

SD_Block_Claim区分FATFS和RAW客户并绑定当前任务。其他任务/模式不得同时访问。未来MSC必须先关闭全部文件、同步、卸载并释放后再取得块设备；本阶段没有MSC切换实现。不得在ISR中操作文件系统。

打开模式READ、WRITE、CREATE_NEW、APPEND；CREATE_NEW拒绝覆盖。路径为0:/，ASCII和8.3名。Write必须检查实际写入字节，FR_OK仍可能因卷满而短写。Read遇EOF允许短读。失败保留FatFS原码、底层首错及清理结果。故障后旧句柄失效，显式Recover不等于数据保存成功。挂载时只读检查分区和BPB，确认实际FAT卷后才开放授权写入。

## 内存和时间

复用08B32KiB、32字节对齐AXI IDMA缓冲，地址0x24000000，未修改MPU/Scatter或SDMMC传输架构。文件系统对象和文件槽静态分配，无动态堆。独立验收的两块128KiB+37 CPU缓冲仅在验收镜像0x24010000至0x24060000区域，正式镜像不包含。

FAT12/16/32、512字节逻辑扇区；禁用mkfs、fdisk、trim、exFAT、LFN和多任务FatFS内部锁。单任务所有权在外层强制。固定无RTC日期2026-11-08。读每次块请求2000ms，写3000ms，恢复可能额外耗时；文件系统分配扫描并非有固定总时限。

## 对外接口

详见CM7/Platform/storage_fs.h。Mount选择分区并请求只读/可写；Unmount拒绝未关闭文件；Recover处理故障并废弃旧句柄；GetDiagnostics返回完整本地诊断副本。GetSpace可能扫描FAT，不应在实时视频循环中高频调用。Sync/Close成功不是抗突然断电承诺。
