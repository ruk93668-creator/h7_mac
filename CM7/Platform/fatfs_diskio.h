#ifndef CM7_FATFS_DISKIO_H
#define CM7_FATFS_DISKIO_H

#include "sys_result.h"
#include <stdbool.h>
#include <stdint.h>

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
