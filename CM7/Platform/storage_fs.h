#ifndef CM7_STORAGE_FS_H
#define CM7_STORAGE_FS_H

#include "sys_result.h"
#include <stdint.h>
#include <stdbool.h>

//卷布局识别结果，GPT标记不代表已经验证或支持GPT
#define STORAGE_FS_LAYOUT_UNKNOWN 0U
#define STORAGE_FS_LAYOUT_DIRECT  1U
#define STORAGE_FS_LAYOUT_MBR     2U
#define STORAGE_FS_LAYOUT_GPT_MARKER 3U
#define STORAGE_FS_LAYOUT_EXFAT   4U
#define STORAGE_FS_LAYOUT_INVALID 5U

#define STORAGE_FS_OPEN_READ       1U
#define STORAGE_FS_OPEN_WRITE      2U
#define STORAGE_FS_OPEN_CREATE_NEW 4U
#define STORAGE_FS_OPEN_APPEND     8U

//文件凭据包含槽位和代号，不向应用暴露FIL指针
typedef uint32_t StorageFile_t;

//诊断保留文件系统原错和底层首错，不混成一个成功标志
typedef struct
{
    uint32_t state;//0未准备、1待挂载、2已挂载、3故障
    uint32_t partition;//0为直接卷，1至4为MBR分区
    uint32_t first_lba;//本轮实际卷起点
    uint32_t sector_count;//BPB检查后的卷扇区数
    uint32_t filesystem;//FatFS实际识别类型，0表示尚未成功识别
    uint32_t layout;//布局标记，不把分区类型当文件系统证明
    uint32_t fatfs_called;//本次是否实际调用过FatFS
    uint32_t session;//成功挂载代号，溢出时拒绝继续复用
    uint32_t open_files;//仍有效的文件对象数量
    uint32_t writable;//本轮挂载的写权限
    uint32_t cid[4];//当前识卡得到的身份
    uint32_t fatfs_result;//最近实际FatFS返回值
    SysResult_t block_result;//本次底层原始错误
    SysResult_t last_result;//最近文件操作结果
    SysResult_t cleanup_result;//释放或恢复的独立结果
    uint32_t data_may_be_lost;//已发生可能丢失未同步数据的错误
} StorageFsDiagnostics_t;

//准备静态文件对象，Initcall只准备资源，不访问卡
SysResult_t StorageFS_Init(void);

//由唯一任务挂载卷，partition为0时只读检查并选择首个FAT分区
SysResult_t StorageFS_Mount(bool writable, uint8_t partition);

//当前任务卸载已关闭全部文件的卷，不强制丢弃脏数据
SysResult_t StorageFS_Unmount(void);

//显式恢复故障卷，旧文件凭据全部失效，不代表文件保存成功
SysResult_t StorageFS_Recover(void);

//打开0:/下的文件，CREATE_NEW拒绝覆盖已有文件
SysResult_t StorageFS_Open(const char *path, uint32_t mode, StorageFile_t *file);

//读取文件并返回实际长度，正常EOF可以短读
SysResult_t StorageFS_Read(StorageFile_t file, void *data, uint32_t size, uint32_t *read_bytes);

//写入并保留实际进度，写满或部分失败不能伪装全部成功
SysResult_t StorageFS_Write(StorageFile_t file, const void *data, uint32_t size, uint32_t *written_bytes);

//同步当前文件的缓存与元数据，失败不代表数据已落卡
SysResult_t StorageFS_Sync(StorageFile_t file);

//关闭当前文件，成功后才能复用该槽位
SysResult_t StorageFS_Close(StorageFile_t file);

//设置文件位置，只读文件不允许定位到文件末尾之外
SysResult_t StorageFS_Seek(StorageFile_t file, uint32_t offset);

//读取文件大小，不更改文件位置
SysResult_t StorageFS_GetSize(StorageFile_t file, uint32_t *size);

//显式查询卷空间，可能扫描FAT，不在视频循环中自动调用
SysResult_t StorageFS_GetSpace(uint64_t *total_bytes, uint64_t *free_bytes);

//创建指定目录，名称仍受8.3规则约束
SysResult_t StorageFS_Mkdir(const char *path);

//显式删除指定文件或空目录，不执行自动清理
SysResult_t StorageFS_Remove(const char *path);

//普通上下文读取完整诊断副本，正在操作时返回BUSY
SysResult_t StorageFS_GetDiagnostics(StorageFsDiagnostics_t *diagnostics);

#endif
