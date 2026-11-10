#ifndef CM7_FATFS_DISKIO_H
#define CM7_FATFS_DISKIO_H

#include "sys_result.h"
#include <stdbool.h>
#include <stdint.h>

//按FatFS请求统计，1到64扇区分别计数，大于64另记
#define FATFS_WRITE_HISTOGRAM_MAX 64U

//只读观察卷布局和实际下发请求，不参与访问权限或错误恢复
typedef struct
{
    uint32_t session;//每次配置卷递增，计数仅属于本次配置
    uint32_t layout_valid;//1表示挂载成功后取得布局
    uint32_t sector_bytes;//逻辑扇区字节数
    uint32_t sectors_per_cluster;//每簇扇区数
    uint32_t cluster_bytes;//每簇字节数
    uint32_t filesystem_type;//FatFS类型编号
    uint32_t volume_sectors;//本卷边界，不是整张卡的容量
    uint32_t write_calls;//真正下发SD_Block_Write的请求次数
    uint32_t success_calls;//完整成功的请求次数
    uint32_t failed_calls;//失败请求次数，不代表完全没有写入
    uint32_t in_flight;//1表示当前请求尚未返回
    uint32_t single_calls;//单扇区请求次数
    uint32_t multi_calls;//多扇区请求次数
    uint32_t min_sectors;//单次请求最少扇区数，0表示无样本
    uint32_t max_sectors;//单次请求最多扇区数
    uint32_t last_sectors;//最近请求扇区数
    uint32_t last_lba;//最近请求起始LBA
    uint32_t average_sectors_milli;//平均每次扇区数乘1000
    uint32_t over64_calls;//超过64扇区的FatFS请求次数
    uint32_t saturated;//1表示次数达到上限后停止累计
    uint64_t requested_sectors;//已下发请求的总扇区数，包含失败请求
    uint64_t successful_sectors;//仅累计完整成功请求的扇区数
    uint32_t sectors_histogram[FATFS_WRITE_HISTOGRAM_MAX + 1U];//下标为扇区数，0不使用
} FatFsDiskWriteDebug_t;

//单一文件系统任务写入，卸载保留结果，下一次配置卷时清零计数
extern volatile FatFsDiskWriteDebug_t g_sd_write_debug;

//记录每次调用的墙钟耗时，包含阻塞和被其他任务抢占的时间
typedef struct
{
    uint32_t samples;//实际调用次数，包含失败
    uint32_t last_ms;//最近一次耗时
    uint32_t max_ms;//最大耗时
    uint32_t average_ms;//平均耗时，整数毫秒
    uint64_t total_ms;//累计耗时，用于按帧数折算
} FatFsIoTimingSample_t;

//上下层统计存在包含关系，不能直接把所有字段相加
typedef struct
{
    FatFsIoTimingSample_t input_prepare;//输入复制及原有安全等待
    FatFsIoTimingSample_t fatfs_write;//完整f_write，包含下面的块设备调用
    FatFsIoTimingSample_t fatfs_other;//f_write扣除嵌套块读、块写和同步
    FatFsIoTimingSample_t block_write;//SD_Block_Write，包含AXI复制、IDMA和卡状态等待
    FatFsIoTimingSample_t block_read;//SD_Block_Read，包含分配及缓存读取
    FatFsIoTimingSample_t block_sync;//SD_Block_Sync检查，不额外触发同步
    FatFsIoTimingSample_t file_sync;//显式f_sync，包含内部块操作
    FatFsIoTimingSample_t file_open;//f_open，包括新建和重名尝试
    FatFsIoTimingSample_t file_close;//f_close，包含内部同步
    FatFsIoTimingSample_t file_seek;//f_lseek，包括AVI回填定位
} FatFsIoTimingDebug_t;

//唯一文件系统任务累计，配置新卷时清零，卸载后保留
extern volatile FatFsIoTimingDebug_t g_storage_io_timing;

//只累计诊断样本，不读写磁盘，不用于超时和资源判断
void FatFsDisk_RecordTiming(volatile FatFsIoTimingSample_t *sample, uint32_t elapsed);

//设置唯一卷映射和允许访问的卷范围，不执行写入
SysResult_t FatFsDisk_Configure(uint8_t partition, uint32_t first, uint32_t count);

//只有检查通过的卷才允许显式开启文件写入
SysResult_t FatFsDisk_SetWritable(bool writable);

//关闭写入口并清理本地卷映射，不停止底层硬件
void FatFsDisk_Reset(void);

//开始一次文件操作的错误记录，不覆盖之前的卷故障状态
void FatFsDisk_Begin(void);

//读取本次FatFS调用遇到的首个块设备错误
SysResult_t FatFsDisk_GetLastResult(void);

#endif
