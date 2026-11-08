#ifndef FATFS_HARDWARE_ACCEPTANCE_H
#define FATFS_HARDWARE_ACCEPTANCE_H
#include "storage_fs.h"

//仅独立验收镜像提供，不注册到正常固件
#define FATFS_ACCEPT_WRITE_CONFIRM 0x46535752UL

typedef struct
{
    uint32_t ready;
    uint32_t request;
    uint32_t state;
    uint32_t phase;
    uint32_t file_index;
    uint32_t write_confirm;
    uint32_t expected_cid[4];
    uint32_t expected_first_lba;
    uint32_t expected_sector_count;
    uint64_t expected_capacity;
    uint64_t capacity;
    uint32_t bytes_written;
    uint32_t bytes_read;
    uint32_t write_crc;
    uint32_t read_crc;
    uint32_t buffer_valid;
    uint32_t stack_free_words;
    SysResult_t result;
    SysResult_t cleanup_result;
    StorageFsDiagnostics_t volume;
} FatFsAcceptance_t;

extern volatile FatFsAcceptance_t g_fatfs_acceptance;

//只在受控验收任务执行一次请求，不自动写卡
void FatFsAcceptance_Process(uint32_t request);
#endif
