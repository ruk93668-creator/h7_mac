//用同一Timer验证两种节奏。跨过20FPS截止点时保持旧帧。
static uint32_t Ui20FpsCase(void)
{
    uint32_t i;
    CHECK(Init() == 0U);
    Step(49U);
    CHECK(s_tx_count == 0U);
    Step(50U);
    CHECK(s_tx_count == 0U && s_view_calls == 0U);
    Step(99U);
    CHECK(s_tx_count == 0U);
    Step(100U);
    CHECK(s_tx_count == 1U && s_view_calls == 1U);
    for (i = 0U; i < 8U; i++)
        Complete(SYS_OK);

    Ui2Post(OLED_UI_EVENT_OPEN_MENU);
    Step(149U);
    CHECK(s_tx_count == 8U);
    Step(150U);
    CHECK(s_tx_count == 9U && s_view_calls == 2U);
    for (i = 0U; i < 8U; i++)
        Complete(SYS_OK);

    Step(200U);
    CHECK(s_tx_count == 17U && s_view_calls == 3U);
    memcpy(s_saved, s_oled.framebuffer, OLED_BUFFER_SIZE);
    Step(250U);
    CHECK(s_tx_count == 17U && s_view_calls == 3U);
    CHECK(memcmp(s_saved, s_oled.framebuffer, OLED_BUFFER_SIZE) == 0);
    CHECK(g_oled_monitor_debug.busy_skips == 1U);
    for (i = 0U; i < 8U; i++)
        Complete(SYS_OK);

    Step(300U);
    CHECK(s_tx_count == 25U && s_view_calls == 4U);
    for (i = 0U; i < 8U; i++)
        Complete(SYS_OK);
    CHECK(g_oled_monitor_debug.completed_frames == 4U);

    //清除仿真传输日志的容量用量，保留真实的应用诊断和Timer。
    s_tx_count = 0U;
    Ui2Post(OLED_UI_EVENT_BACK);
    Step(350U);
    CHECK(s_tx_count == 1U && s_view_calls == 5U);
    for (i = 0U; i < 8U; i++)
        Complete(SYS_OK);
    Step(400U);
    CHECK(s_tx_count == 8U && s_view_calls == 5U);
    Step(450U);
    CHECK(s_tx_count == 9U && s_view_calls == 6U);
    CHECK(s_timer_pool_count == 1U && s_heap_size == 1U);
    return 0U;
}
