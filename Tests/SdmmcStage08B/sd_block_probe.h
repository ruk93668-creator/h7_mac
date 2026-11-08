#ifndef SD_BLOCK_PROBE_H
#define SD_BLOCK_PROBE_H

#include "bsp_sdmmc.h"

//默认不创建验收任务，也不初始化或访问卡
#ifndef APP_SD_BLOCK_TEST_ENABLED
#define APP_SD_BLOCK_TEST_ENABLED 0
#endif

//真实写卡必须单独开启，并完整填写测试卡身份和独立原始区域
#ifndef APP_SD_BLOCK_WRITE_TEST_ENABLED
#define APP_SD_BLOCK_WRITE_TEST_ENABLED 0
#endif

#if (APP_SD_BLOCK_TEST_ENABLED != 0 && APP_SD_BLOCK_TEST_ENABLED != 1) || \
    (APP_SD_BLOCK_WRITE_TEST_ENABLED != 0 && APP_SD_BLOCK_WRITE_TEST_ENABLED != 1)
#error SD_block_test_switches_must_be_zero_or_one
#endif

#if APP_SD_BLOCK_WRITE_TEST_ENABLED
#if !APP_SD_BLOCK_TEST_ENABLED
#error write_test_requires_probe_enabled
#endif
#if !defined(SD_TEST_CID0) || !defined(SD_TEST_CID1) || !defined(SD_TEST_CID2) || !defined(SD_TEST_CID3) || \
    !defined(SD_TEST_CAPACITY_BYTES) || !defined(SD_TEST_WRITE_FIRST_LBA) || !defined(SD_TEST_WRITE_BLOCKS)
#error write_test_requires_explicit_card_identity_capacity_and_raw_region
#endif
#if SD_TEST_WRITE_FIRST_LBA == 0 || SD_TEST_WRITE_BLOCKS < 8
#error write_test_must_not_include_LBA0_and_requires_eight_reserved_blocks
#endif
#endif

//保存受控验收请求和结果，不作为正式存储服务
//request=1只读，2授权写入，3恢复原数据，4恢复外设并重新识卡
//2和3仅在写测试启用时支持，4不会自动执行写入或丢弃原数据备份
typedef struct
{
    uint32_t enabled;
    uint32_t request;
    uint32_t state;//0未运行，1处理中，2成功，3失败
    uint32_t read_crc;
    uint32_t repeat_crc;
    uint32_t restore_required;
    SysResult_t result;
    SysResult_t restore_result;
    uint32_t cid[4];
    BspSdmmcCardInfo_t card;
    BspSdmmcDiagnostics_t init;
    BspSdmmcIoDiagnostics_t io;
    uint32_t stack_free_words;
} SdBlockProbe_t;

//供Keil Watch观察，默认不开启扇区测试
extern volatile SdBlockProbe_t g_sd_block_test;

#endif
