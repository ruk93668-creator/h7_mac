static uint32_t Ui6Case(uint32_t id)
{
    static const uint8_t events[] =
    {
        OLED_UI_EVENT_OPEN_MENU, OLED_UI_EVENT_SELECT, OLED_UI_EVENT_SELECT,
        OLED_UI_EVENT_SELECT, OLED_UI_EVENT_BACK, OLED_UI_EVENT_BACK,
        OLED_UI_EVENT_BACK, OLED_UI_EVENT_BACK
    };
    static const uint8_t pages[] =
    {
        OLED_UI_PAGE_SYSTEM, OLED_UI_PAGE_CAMERA, OLED_UI_PAGE_CAMERA_STATUS,
        OLED_UI_PAGE_CAMERA_STATUS, OLED_UI_PAGE_CAMERA_STATUS, OLED_UI_PAGE_CAMERA,
        OLED_UI_PAGE_SYSTEM, OLED_UI_PAGE_MONITOR
    };
    uint32_t i, page, now = 0xfffffe00U;
    (void)id;
    s_now = now;
    CHECK(Init() == 0U);

    for (i = 0U; i < 6000U; i++)
    {
        s_tx_count = 0U;
        s_view.peer_state = (i % 31U < 4U) ? IPC_PEER_TIMEOUT : IPC_PEER_ONLINE;
        s_view.fresh_mask = (i % 17U == 0U) ? 0U : IPC_APP_ALL;
        now += 100U;
        Step(now);
        CHECK(s_oled.refresh_state == OLED_REFRESH_WAIT_CMD && s_tx_count == 1U);
        memcpy(s_saved, s_oled.framebuffer, OLED_BUFFER_SIZE);
        Ui2Post(events[i % sizeof(events)]);
        CHECK(s_ui2_state.current == pages[i % sizeof(pages)]);
        CHECK(g_oled_ui2_debug.ack == i + 1U && g_oled_ui2_debug.result == SYS_OK);

        for (page = 0U; page < 4U; page++)
        {
            CHECK(s_tx_len[page * 2U] == 4U && s_tx[page * 2U][1] == (0xB0U | page));
            Complete(SYS_OK);
            CHECK(s_oled.refresh_state == OLED_REFRESH_WAIT_DATA);
            CHECK(s_tx_len[page * 2U + 1U] == 129U);
            CHECK(s_tx[page * 2U + 1U][0] == 0x40U);
            CHECK(memcmp(&s_tx[page * 2U + 1U][1], &s_saved[page * OLED_WIDTH], OLED_WIDTH) == 0);
            CHECK(memcmp(s_saved, s_oled.framebuffer, OLED_BUFFER_SIZE) == 0);
            Complete(SYS_OK);
            CHECK(memcmp(s_saved, s_oled.framebuffer, OLED_BUFFER_SIZE) == 0);
        }

        CHECK(s_oled.refresh_state == OLED_REFRESH_IDLE && s_tx_count == 8U);
        CHECK(g_oled_monitor_debug.completed_frames == i + 1U);
    }
    CHECK(g_oled_monitor_debug.io_errors == 0U && g_oled_monitor_debug.render_errors == 0U);
    return 0U;
}
