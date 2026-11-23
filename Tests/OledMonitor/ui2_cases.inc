//UI2测试使用真实状态机、Device绘图和Application刷新事务。
static void Ui2Post(uint32_t event)
{
    g_oled_ui2_debug.event = event;
    g_oled_ui2_debug.request++;
    App_OledRun();
}

static uint32_t Ui2Case(uint32_t id)
{
    OledUiPageState_t state, saved;
    const OledUiPage_t *page, *parent;
    const OledUiMenuItem_t *item;
    OledDevice_t *oled = &s_guard.oled;
    uint32_t i, j, depth;
    uint8_t frame[OLED_BUFFER_SIZE];

    CHECK(OledUi_PageInit(&state) == SYS_OK);
    if (id == 69U)
    {
        CHECK(OledUi_PageGetCurrent(&state, &page) == SYS_OK);
        CHECK(page->id == OLED_UI_PAGE_MONITOR && state.revision == 0U);
        CHECK(OledUi_PageBack(&state) == SYS_OK && state.revision == 0U);
        for (i = 0U; i < OLED_UI_PAGE_COUNT; i++)
        {
            CHECK(OledUi_PageGet((OledUiPageId_t)i, &page) == SYS_OK);
            CHECK(page->id == i && page->count <= OLED_UI_MENU_CAPACITY);
            CHECK((page->items == NULL) == (page->count == 0U));
            for (parent = page, depth = 0U; parent->id != OLED_UI_PAGE_MONITOR; depth++)
            {
                CHECK(depth < 3U);
                CHECK(OledUi_PageGet(parent->parent, &parent) == SYS_OK);
            }
            for (j = 0U; j < page->count; j++)
            {
                item = &page->items[j];
                CHECK(OledUi_PageGet(item->target, &parent) == SYS_OK);
                CHECK(item->action == OLED_UI_ITEM_OPEN ? parent->parent == page->id :
                    item->action == OLED_UI_ITEM_BACK && item->target == page->parent);
            }
        }
        saved = state;
        CHECK(OledUi_PageOpen(&state, OLED_UI_PAGE_COUNT) == SYS_ERR_INVALID_ARG);
        CHECK(OledUi_PageOpen(&state, OLED_UI_PAGE_NETWORK) == SYS_ERR_INVALID_STATE);
        CHECK(OledUi_PageDispatch(&state, 0xffffffffU) == SYS_ERR_INVALID_ARG);
        CHECK(memcmp(&state, &saved, sizeof(state)) == 0);
        state.cursor[OLED_UI_PAGE_SYSTEM].first_visible = 5U;
        saved = state;
        CHECK(OledUi_PageOpen(&state, OLED_UI_PAGE_SYSTEM) == SYS_ERR_INVALID_STATE);
        CHECK(memcmp(&state, &saved, sizeof(state)) == 0);
    }
    else if (id == 70U)
    {
        CHECK(OledUi_PageDispatch(&state, OLED_UI_EVENT_OPEN_MENU) == SYS_OK);
        CHECK(OledUi_PageMovePrevious(&state) == SYS_OK && state.revision == 1U);
        for (i = 0; i < 100U; i++)
            CHECK(OledUi_PageMoveNext(&state) == SYS_OK);
        CHECK(state.cursor[state.current].selected == 4U);
        CHECK(state.cursor[state.current].first_visible == 2U && state.revision == 5U);
        CHECK(OledUi_PageMovePrevious(&state) == SYS_OK);
        CHECK(OledUi_PageDispatch(&state, OLED_UI_EVENT_SELECT) == SYS_OK);
        CHECK(state.current == OLED_UI_PAGE_ABOUT);
        saved = state;
        CHECK(OledUi_PageGetSelected(&state, &item) == SYS_ERR_NOT_FOUND);
        CHECK(OledUi_PageDispatch(&state, OLED_UI_EVENT_SELECT) == SYS_OK);
        CHECK(OledUi_PageMoveNext(&state) == SYS_OK && memcmp(&state,&saved,sizeof(state)) == 0);
        CHECK(OledUi_PageBack(&state) == SYS_OK && state.current == OLED_UI_PAGE_SYSTEM);
        CHECK(state.cursor[state.current].selected == 3U && state.cursor[state.current].first_visible == 2U);
        CHECK(OledUi_PageOpen(&state, OLED_UI_PAGE_CAMERA) == SYS_OK);
        CHECK(OledUi_PageMoveNext(&state) == SYS_OK);
        CHECK(OledUi_PageDispatch(&state, OLED_UI_EVENT_SELECT) == SYS_OK);
        CHECK(state.current == OLED_UI_PAGE_CAMERA_FORMAT);
        CHECK(OledUi_PageBack(&state) == SYS_OK && state.cursor[state.current].selected == 1U);
        CHECK(OledUi_PageBack(&state) == SYS_OK);
        CHECK(OledUi_PageOpen(&state, OLED_UI_PAGE_NETWORK) == SYS_OK);
        saved = state;
        CHECK(OledUi_PageMoveNext(&state) == SYS_OK);
        CHECK(OledUi_PageMovePrevious(&state) == SYS_OK && memcmp(&state,&saved,sizeof(state)) == 0);
        CHECK(OledUi_PageDispatch(&state, OLED_UI_EVENT_SELECT) == SYS_OK && state.current == OLED_UI_PAGE_SYSTEM);
    }
    else if (id == 71U)
    {
        oled->base.state = DEVICE_STATE_READY;
        oled->ops = &s_test_ops;
        CHECK(OledUi_PageOpen(&state, OLED_UI_PAGE_SYSTEM) == SYS_OK);
        CHECK(OledUi_MenuRender(oled, &state) == SYS_OK);
        CHECK(oled->framebuffer[0] == 0U && oled->framebuffer[3] == 0x46);
        CHECK(oled->framebuffer[255] == 0xffU && oled->framebuffer[383] == 0U);
        CHECK(oled->framebuffer[128 + 15] == 0xc1U);
        CHECK(oled->framebuffer[256 + 15] == 0x7fU);
        for (i = 0U; i < 4U; i++)
            CHECK(OledUi_PageMoveNext(&state) == SYS_OK);
        CHECK(OledUi_MenuRender(oled, &state) == SYS_OK);
        CHECK(oled->framebuffer[255] == 0U && oled->framebuffer[511] == 0xffU);
        CHECK(oled->framebuffer[128 + 15] == 0x46U);
        CHECK(oled->framebuffer[384 + 15] == 0x80U);
        memcpy(frame, oled->framebuffer, sizeof(frame));
        oled->refresh_state = OLED_REFRESH_WAIT_CMD;
        CHECK(OledUi_MenuRender(oled, &state) == SYS_ERR_BUSY);
        oled->refresh_state = OLED_REFRESH_WAIT_DATA;
        CHECK(OledUi_MenuRender(oled, &state) == SYS_ERR_BUSY);
        CHECK(memcmp(frame, oled->framebuffer, sizeof(frame)) == 0);
    }
    else
    {
        CHECK(Init() == 0U);
        if (id == 72U)
        {
            Step(100);
            CHECK(s_tx_count == 1U && s_ui2_state.current == OLED_UI_PAGE_MONITOR);
            memcpy(s_saved, s_oled.framebuffer, OLED_BUFFER_SIZE);
            Ui2Post(OLED_UI_EVENT_OPEN_MENU);
            CHECK(s_oled.refresh_state == OLED_REFRESH_WAIT_CMD);
            CHECK(g_oled_ui2_debug.ack == 1U && s_ui2_state.current == OLED_UI_PAGE_SYSTEM);
            s_active = 0U; s_ready = 1U; s_completion = SYS_OK;
            App_OledRun();
            CHECK(s_oled.refresh_state == OLED_REFRESH_WAIT_DATA);
            Ui2Post(OLED_UI_EVENT_NEXT);
            CHECK(g_oled_ui2_debug.ack == 2U && s_ui2_state.cursor[OLED_UI_PAGE_SYSTEM].selected == 1U);
            CHECK(memcmp(s_saved, s_oled.framebuffer, OLED_BUFFER_SIZE) == 0);
            for (i = 1U; i < 8U; i++)
            {
                Complete(SYS_OK);
                CHECK(memcmp(s_saved, s_oled.framebuffer, OLED_BUFFER_SIZE) == 0);
            }
            CHECK(s_oled.refresh_state == OLED_REFRESH_IDLE);
            Step(200);
            CHECK(s_oled.framebuffer[383] == 0xffU);
            memcpy(s_saved, s_oled.framebuffer, OLED_BUFFER_SIZE);
            for (i=0U; i<8U; i++) Complete(SYS_OK);
            Ui2Post(OLED_UI_EVENT_BACK);
            Step(300);
            CHECK(s_ui2_state.current == OLED_UI_PAGE_MONITOR && s_oled.framebuffer[383] == 0U);
        }
        else
        {
            Ui2Post(OLED_UI_EVENT_OPEN_MENU);
            s_oled.ops = &s_test_ops;
            memset(s_oled.framebuffer, 0x7d, OLED_BUFFER_SIZE);
            memcpy(frame, s_oled.framebuffer, sizeof(frame));
            if (id == 73U)
                s_fail_pixel = 30U;
            else
                s_submit_result = id == 74U ? SYS_ERR_BUSY : SYS_ERR_IO;
            Step(100);
            CHECK(s_tx_count == 0U && s_oled.refresh_state == OLED_REFRESH_IDLE);
            CHECK(memcmp(frame,s_oled.framebuffer,sizeof(frame)) == 0);
            CHECK(s_ui2_state.current == OLED_UI_PAGE_SYSTEM);
            if (id == 73U)
                CHECK(g_oled_monitor_debug.render_errors == 1U);
            else
                CHECK(g_oled_monitor_debug.last_result == s_submit_result);
            s_fail_pixel = 0U;
            s_submit_result = SYS_OK;
            Step(200);
            CHECK(s_tx_count == 1U && s_oled.framebuffer[255] == 0xffU);
        }
    }
    return 0U;
}
