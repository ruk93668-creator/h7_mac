#ifndef CM7_FFCONF_H
#define CM7_FFCONF_H

//单卷FAT读写配置，不提供格式化、长文件名或动态内存
#define FFCONF_DEF 80386
#define FF_FS_READONLY 0 //保留读写API，运行期仍默认只读挂载
#define FF_FS_MINIMIZE 0
#define FF_USE_FIND 0
#define FF_USE_MKFS 0 //不提供自动格式化或分区
#define FF_USE_FASTSEEK 0
#define FF_USE_EXPAND 0
#define FF_USE_CHMOD 0
#define FF_USE_LABEL 0
#define FF_USE_FORWARD 0
#define FF_USE_STRFUNC 0
#define FF_PRINT_LLI 0
#define FF_PRINT_FLOAT 0
#define FF_STRF_ENCODE 0
#define FF_CODE_PAGE 437 //ASCII文件名使用默认OEM页
#define FF_USE_LFN 0 //本阶段使用ASCII 8.3文件名
#define FF_MAX_LFN 255
#define FF_LFN_UNICODE 0
#define FF_LFN_BUF 255
#define FF_SFN_BUF 12
#define FF_FS_RPATH 0
#define FF_PATH_DEPTH 10
#define FF_VOLUMES 1 //只管理一个卷
#define FF_STR_VOLUME_ID 0
#define FF_VOLUME_STRS "RAM","NAND","CF","SD","SD2","USB","USB2","USB3"
#define FF_MULTI_PARTITION 1 //由预检查指定MBR分区，避免重复偏移
#define FF_MIN_SS 512 //固定512字节逻辑扇区
#define FF_MAX_SS 512 //不支持其他扇区大小
#define FF_LBA64 0 //沿用现有32位LBA块接口
#define FF_MIN_GPT 0x10000000
#define FF_USE_TRIM 0 //不下发额外擦除命令
#define FF_FS_TINY 0 //每个静态文件对象保留扇区缓存
#define FF_FS_EXFAT 0 //本阶段不支持exFAT
#define FF_FS_NORTC 1 //没有已接入RTC，不伪造实时时间
#define FF_NORTC_MON 11
#define FF_NORTC_MDAY 8
#define FF_NORTC_YEAR 2026 //固定文件时间，不是实际拍摄时间
#define FF_FS_CRTIME 0
#define FF_FS_NOFSINFO 0
#define FF_FS_LOCK 4 //四个文件对象的共享检查
#define FF_FS_REENTRANT 0 //只有唯一任务所有者，不另建FatFS互斥层
#define FF_FS_TIMEOUT 1000

#endif
