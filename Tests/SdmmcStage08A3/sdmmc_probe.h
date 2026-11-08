#ifndef SDMMC_STAGE08A3_PROBE_H
#define SDMMC_STAGE08A3_PROBE_H

#include "bsp_sdmmc.h"

//默认不访问SD卡，只有显式开启才执行一次冷启动验收
#ifndef APP_SDMMC_TEST_ENABLED
#define APP_SDMMC_TEST_ENABLED 0
#endif

//第一次只看PH14电平，插拔确认后改为0执行完整识卡
#ifndef APP_SDMMC_DETECT_ONLY
#define APP_SDMMC_DETECT_ONLY 1
#endif

#if (APP_SDMMC_TEST_ENABLED != 0 && APP_SDMMC_TEST_ENABLED != 1) ||     (APP_SDMMC_DETECT_ONLY != 0 && APP_SDMMC_DETECT_ONLY != 1)
#error SDMMC_probe_switches_must_be_zero_or_one
#endif

//测试专用诊断不进入共享内存，不由其他业务任务访问
typedef struct
{
    uint32_t enabled;//本镜像是否启用测试
    uint32_t detect_only;//是否仅验证检测输入，而未执行识卡
    uint32_t attempted;//是否已经执行本次冷启动测试
    uint32_t finished;//测试入口是否已返回
    uint32_t detect_raw;//PH14原始输入，插拔时应发生变化
    uint32_t card_present;//按当前编译极性解释的插卡状态
    SysResult_t result;//本次测试真实结果，不传播为Framework启动失败
    SysResult_t diagnostics_result;//取得诊断副本的结果
    BspSdmmcDiagnostics_t diagnostics;//BSP完整诊断
    BspSdmmcCardInfo_t card;//仅完整识卡成功后才有效
} SdmmcTest_t;

//在Keil Watch观察测试结果，不通过调试器直接改HAL句柄
extern volatile SdmmcTest_t g_sdmmc_test;

//执行一次受开关控制的测试，始终不阻断Framework后续初始化
SysResult_t SdmmcTest_Run(void);

#endif
