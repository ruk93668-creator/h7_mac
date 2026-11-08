//使用实际测试入口，核对C和D只在任务创建行为上不同
#include "adapter_test.c"

//保留压力调用链需要时间接口，但D组不能实际执行压力
uint32_t Platform_TimeGetMs(void)
{
    return s_tick;
}

//验证启动门控不创建D组任务，也不因Watch请求自动执行读卡
uint32_t SdBlockDGroup_TestRun(uint32_t id)
{
    Setup();

    //预置请求或创建故障，检查D组启动仍然没有硬件操作
    if (id == 1U)
        g_sd_block_test.request = 5U;
    if (id == 2U)
        s_sd_test_task_fail = 1U;

    CHECK(SdBlockTest_Prepare() == SYS_OK);
    CHECK(g_sd_block_test.stress.enabled == 1U);
    CHECK(g_sd_block_test.stress.active == 0U);
    CHECK(g_sd_block_d_group == APP_SD_BLOCK_D_GROUP_ENABLED);

    //D组必须完全跳过xTaskCreateStatic；C组保留原创建与失败处理
    if (APP_SD_BLOCK_D_GROUP_ENABLED)
    {
        CHECK(s_sd_test_task_creates == 0U);
        CHECK(g_sd_block_test.enabled == 0U);
        CHECK(g_sd_block_test.result == SYS_OK);
    }
    else
    {
        CHECK(s_sd_test_task_creates == 1U);
        CHECK(g_sd_block_test.enabled == (id == 2U ? 0U : 1U));
        CHECK(g_sd_block_test.result == (id == 2U ? SYS_ERR_NO_RESOURCE : SYS_OK));
    }

    //两组准备阶段均不得读卡、启动IDMA、延迟等待或修改压力哨兵
    CHECK(s_calls == 0U);
    CHECK(s_starts == 0U);
    CHECK(s_waits == 0U);
    CHECK(s_stress_buffer.before == 0U && s_stress_buffer.after == 0U);
    CHECK(g_sd_block_test.request == (id == 1U ? 5U : 0U));
    return 0U;
}
