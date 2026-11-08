//执行真实测试入口和等待函数，不运行SD硬件事务
#include "adapter_test.c"

//保留压力链接引用所需的时间接口
uint32_t Platform_TimeGetMs(void)
{
    return s_tick;
}

//验证同一任务内修改等待周期以及无读写保护
uint32_t SdBlockPoll_TestRun(uint32_t id)
{
    uint32_t expected = 20U;
    uint32_t request;

    Setup();
    CHECK(SdBlockTest_Prepare() == SYS_OK);
    CHECK(s_sd_test_task_creates == 1U);
    CHECK(g_sd_block_test.enabled == 1U);
    CHECK(g_sd_block_d_group == 0U && g_sd_block_poll_only == 1U);
    CHECK(g_sd_block_test.poll_period_ms == 20U);

    //在同次执行里经历20、1000、20ms，不重建任务或资源
    if (id == 2U)
    {
        SdBlockTest_PollDelay();
        CHECK(s_test_last_delay == pdMS_TO_TICKS(20U));
        g_sd_block_test.poll_period_ms = 1000U;
        SdBlockTest_PollDelay();
        CHECK(s_test_last_delay == pdMS_TO_TICKS(1000U));
        g_sd_block_test.poll_period_ms = 20U;
    }
    else if (id == 1U)
    {
        g_sd_block_test.poll_period_ms = 1000U;
        expected = 1000U;
    }
    else if (id == 3U)
        g_sd_block_test.poll_period_ms = 0U;
    else if (id == 4U)
        g_sd_block_test.poll_period_ms = 19U;
    else if (id == 5U || id == 6U)
    {
        g_sd_block_test.poll_period_ms = id == 5U ? 1001U : UINT32_MAX;
        expected = 1000U;
    }
    else if (id == 7U)
        g_sd_block_test.poll_count = UINT32_MAX;

    SdBlockTest_PollDelay();
    CHECK(g_sd_block_test.poll_effective_ms == expected);
    CHECK(s_test_last_delay == pdMS_TO_TICKS(expected));
    CHECK(g_sd_block_test.poll_count == (id == 7U ? 0U : id == 2U ? 3U : 1U));
    CHECK(s_test_delay_calls == (id == 2U ? 3U : 1U));
    CHECK(s_sd_test_task_creates == 1U);

    //误写任何测试请求均拒绝，不能触发读写、恢复或压力
    if (id == 8U)
    {
        for (request = 1U; request <= 5U; request++)
            CHECK(SdBlockTest_RunRequest(request) == SYS_ERR_NOT_SUPPORTED);
    }
    CHECK(s_calls == 0U);
    CHECK(s_starts == 0U);
    CHECK(s_waits == 0U);
    CHECK(g_sd_block_test.stress.active == 0U);
    return 0U;
}
