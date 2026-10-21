#ifndef CM7_SDRAM_CHECK_H
#define CM7_SDRAM_CHECK_H

#include "sys_result.h"

//首1KB保留为验收区，不交给业务缓存
#define SDRAM_CHECK_BYTES 1024U

//0未运行、1运行中、2通过、3失败
extern volatile uint32_t g_sdram_check_state;
extern volatile SysResult_t g_sdram_check_result;
extern volatile uint32_t g_sdram_check_words;
extern volatile uint32_t g_sdram_check_fail_address;
extern volatile uint32_t g_sdram_check_expected;
extern volatile uint32_t g_sdram_check_actual;

SysResult_t SDRAM_CheckRun(void);

#endif
