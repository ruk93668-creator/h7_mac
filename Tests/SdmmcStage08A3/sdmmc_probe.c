//本文件仅提供受控验收入口，默认固件不初始化SD卡
#include "sdmmc_probe.h"
#include "framework_init.h"

//保存一次冷启动测试的全部结果，默认关闭时保持未执行
volatile SdmmcTest_t g_sdmmc_test;

//执行受控的一次性识卡测试
SysResult_t SdmmcTest_Run(void)
{
#if APP_SDMMC_TEST_ENABLED
    BspSdmmcDiagnostics_t diagnostics = {0};
    BspSdmmcCardInfo_t card = {0};
    uint32_t raw = 0U;
    bool present = false;
    SysResult_t result;

    //同一镜像只执行一次，不用循环重试拖住其他模块启动
    if (g_sdmmc_test.attempted != 0U)
        return SYS_OK;

    g_sdmmc_test.enabled = 1U;
    g_sdmmc_test.detect_only = APP_SDMMC_DETECT_ONLY;
    g_sdmmc_test.attempted = 1U;

    //先保留原始电平，避免用未经核实的极性解释无卡故障
    result = BSP_SDMMC_ReadDetect(&raw, &present);
    g_sdmmc_test.detect_raw = raw;
    g_sdmmc_test.card_present = present ? 1U : 0U;

    //只有完整识卡模式才调用同步HAL，默认仅检测不会访问SD命令线
    if (result == SYS_OK && !APP_SDMMC_DETECT_ONLY)
    {
        result = BSP_SDMMC_Init();
        if (result == SYS_OK)
            result = BSP_SDMMC_GetCardInfo(&card);

        g_sdmmc_test.diagnostics_result = BSP_SDMMC_GetDiagnostics(&diagnostics);
        g_sdmmc_test.diagnostics = diagnostics;
        g_sdmmc_test.card = card;
    }

    //真实失败只记在测试变量，不交给Fail-fast Initcall中止整机启动
    g_sdmmc_test.result = result;
    g_sdmmc_test.finished = 1U;
#else

    //关闭测试时只保留可观察标记，不访问GPIO或SDMMC
    g_sdmmc_test.enabled = 0U;
#endif
    return SYS_OK;
}

//在应用准备阶段进行可选测试，不创建任务或连接Camera/USB控制路径
FRAMEWORK_INIT_REGISTER(SdmmcTest_Run, INIT_LEVEL_APPLICATION);
