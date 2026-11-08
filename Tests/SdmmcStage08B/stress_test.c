//实际执行probe压力函数及BSP/HAL，硬件数据与时间由现有隔离桩驱动
#define SD_STRESS_TEST_HOOKS
#include "adapter_test.c"

//选择本次故障注入场景，不把Mock结果当成实板性能
static uint32_t s_stress_case;

//统计轮间阻塞次数，确认没有忙循环或新建任务
static uint32_t s_stress_delays;

//提供可回绕的本地毫秒时间
uint32_t Platform_TimeGetMs(void)
{
    if (s_stress_case == 9U)
        return 100U;
    return s_tick;
}

//在IDMA事件产生期间注入停止、CRC扰动或边界破坏
void SdStress_TestWaitHook(void)
{
#if APP_SD_BLOCK_STRESS_TEST_ENABLED
    if (s_stress_case == 2U && s_starts == 1U)
        g_sd_block_test.stress.stop_request = 1U;
    if (s_stress_case == 3U && s_starts == 4U)
        g_sdmmc_dma_buffer[0] ^= 1U;
    if (s_stress_case == 10U && s_starts == 2U)
        s_stress_buffer.after = 0U;
#endif
}

//在轮间阻塞时注入到时、停止或下一轮驱动错误
void SdStress_TestDelayHook(TickType_t ticks)
{
    (void)ticks;
    s_stress_delays++;
#if APP_SD_BLOCK_STRESS_TEST_ENABLED
    if (s_stress_case == 1U)
        s_tick += SD_BLOCK_STRESS_DURATION_MS;
    if (s_stress_case == 4U)
        g_sd_block_test.stress.stop_request = 1U;
    if (s_stress_case == 5U)
        s_case = 8U;
    if (s_stress_case == 12U)
    {
        g_sd_block_test.stress.limit_ms = UINT32_MAX;
        g_sd_block_test.stress.max_rounds = UINT32_MAX;
    }
    if (s_stress_case == 15U)
        g_sd_block_test.request = 4U;
#endif
}

//验证压力入口不写卡并按真实退出原因报告结果
uint32_t SdBlockStress_TestRun(uint32_t id)
{
    SysResult_t result;
    uint32_t before;
    s_stress_case = id;
    Setup();
    s_case = 40U;
    CHECK(BSP_SDMMC_MemoryInit() == SYS_OK);
    CHECK(SdBlock_Prepare() == SYS_OK);
    CHECK(SD_Block_Init() == SYS_OK);
    s_sd.SdCard.CardType = CARD_SDHC_SDXC;
    s_sd.SdCard.LogBlockNbr = s_card.logical_block_count;
#if APP_SD_BLOCK_STRESS_TEST_ENABLED
    if (id == 6U)
        g_sd_block_test.restore_required = 1U;
    if (id == 7U)
    {
        CHECK(BSP_SDMMC_DeInit() == SYS_OK);
        s_init_status = HAL_ERROR;
    }
    if (id == 8U)
        s_tick = 0xffffffe0UL;
    if (id == 11U)
        s_case = 11U;
    if (id == 13U)
        g_sd_block_test.stress.active = 1U;
    if (id == 14U)
        s_card.logical_block_count = 64U;
    before = s_starts;
    result = SdBlockTest_RunRequest(5U);
    if (id == 6U)
    {
        CHECK(result == SYS_ERR_INVALID_STATE && s_starts == before);
        CHECK(g_sd_block_test.restore_required == 1U);
        return 0U;
    }
    if (id == 13U)
    {
        CHECK(result == SYS_ERR_BUSY && s_starts == before);
        CHECK(g_sd_block_test.stress.active == 1U);
        return 0U;
    }
    CHECK(g_sd_block_test.stress.active == 0U);
    CHECK(s_io.write == 0U);
    CHECK(g_sd_block_test.request == 0U);
    if (id == 3U || id == 10U)
    {
        CHECK(result == SYS_ERR_PROTOCOL);
        CHECK(g_sd_block_test.stress.stop_reason == (id == 3U?SD_STRESS_STOP_CRC:SD_STRESS_STOP_BUFFER));
        CHECK(g_sd_block_test.stress.crc_errors == (id == 3U?1U:0U));
        CHECK(g_sd_block_test.stress.buffer_errors == (id == 10U?1U:0U));
        CHECK(s_io.fault == 0U);
        return 0U;
    }
    if (id == 5U || id == 11U)
    {
        CHECK(result == (id == 5U?SYS_ERR_IO:SYS_ERR_TIMEOUT));
        CHECK(g_sd_block_test.stress.stop_reason == SD_STRESS_STOP_IO);
        CHECK(g_sd_block_test.stress.io_errors == 1U);
        CHECK(s_io.fault == 1U && s_io.transfer_ready == 0U);
        return 0U;
    }
    if (id == 7U || id == 14U)
    {
        CHECK(result == (id == 7U?SYS_ERR_IO:SYS_ERR_INVALID_ARG));
        CHECK(g_sd_block_test.stress.stop_reason == SD_STRESS_STOP_INIT);
        CHECK(g_sd_block_test.stress.attempted_rounds == 0U);
        return 0U;
    }
    CHECK(result == SYS_OK);
    CHECK(g_sd_block_test.stress.crc_ready == 1U);
    CHECK(g_sd_block_test.stress.reference_crc == g_sd_block_test.stress.last_crc);
    CHECK(g_sd_block_test.stress.io_errors == 0U && g_sd_block_test.stress.crc_errors == 0U);
    CHECK(g_sd_block_test.stress.bytes_read == g_sd_block_test.stress.completed_rounds*SD_BLOCK_STRESS_BYTES);
    CHECK(s_io.active == 0U && s_io.fault == 0U);
    CHECK(s_starts-before == g_sd_block_test.stress.completed_rounds*2U);
    if (id == 1U)
    {
        CHECK(g_sd_block_test.stress.stop_reason == SD_STRESS_STOP_TIME);
        CHECK(g_sd_block_test.stress.completed_rounds == 1U);
        return 0U;
    }
    if (id == 2U || id == 4U)
    {
        CHECK(g_sd_block_test.stress.stop_reason == SD_STRESS_STOP_USER);
        CHECK(g_sd_block_test.stress.completed_rounds == 1U);
        if (id == 2U)
            CHECK(s_stress_delays == 0U);
        return 0U;
    }
    CHECK(g_sd_block_test.stress.stop_reason == SD_STRESS_STOP_ROUNDS);
    CHECK(g_sd_block_test.stress.completed_rounds == SD_BLOCK_STRESS_MAX_ROUNDS);
    CHECK(s_stress_delays != 0U);
    if (id == 9U)
        CHECK(g_sd_block_test.stress.elapsed_ms == 0U && g_sd_block_test.stress.bytes_per_second == 0U);
    if (id == 16U)
    {
        before = s_starts;
        CHECK(SdBlockTest_RunRequest(5U) == SYS_OK);
        CHECK(g_sd_block_test.stress.completed_rounds == SD_BLOCK_STRESS_MAX_ROUNDS);
        CHECK(s_starts-before == SD_BLOCK_STRESS_MAX_ROUNDS*2U);
    }
#else
    (void)before;
    result = SdBlockTest_RunRequest(5U);
    CHECK(result == SYS_ERR_NOT_SUPPORTED);
    CHECK(s_starts == 0U);
#endif
    return 0U;
}
