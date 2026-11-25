//输入队列允许连发，但旧页旋转不能进入刚打开的窗口或局部页。
static uint32_t UiPreMergeCase(void)
{
    uint32_t i;

    CHECK(Init() == 0U);
    CHECK(OledUi_PageOpen(&s_page_state, OLED_UI_PAGE_SYSTEM) == SYS_OK);
    CHECK(OledUi_PageOpen(&s_page_state, OLED_UI_PAGE_TOOLS) == SYS_OK);
    CHECK(OledUi_InputPush(&s_oled_input_queue, OLED_UI_EVENT_SELECT, false) == SYS_OK);
    CHECK(OledUi_InputPush(&s_oled_input_queue, OLED_UI_EVENT_NEXT, false) == SYS_OK);
    CHECK(App_OledConsumeInput() == SYS_OK);
    CHECK(s_oled_tools.modal != 0U && s_oled_tools.draft == 0);
    CHECK(g_oled_ui_dropped_inputs == 1U);

    CHECK(OledUi_InputPush(&s_oled_input_queue, OLED_UI_EVENT_NEXT, false) == SYS_OK);
    CHECK(OledUi_InputPush(&s_oled_input_queue, OLED_UI_EVENT_SELECT, false) == SYS_OK);
    CHECK(OledUi_InputPush(&s_oled_input_queue, OLED_UI_EVENT_NEXT, false) == SYS_OK);
    CHECK(App_OledConsumeInput() == SYS_OK);
    CHECK(s_oled_tools.modal == 0U && s_oled_tools.values[0] == 1);
    CHECK(s_oled_tools.selected == 0U && g_oled_ui_dropped_inputs == 2U);

    for (i = 0U; i < 12U; i++)
        Ui2Post(OLED_UI_EVENT_NEXT);
    CHECK(s_oled_tools.selected == 12U);
    CHECK(OledUi_InputPush(&s_oled_input_queue, OLED_UI_EVENT_SELECT, false) == SYS_OK);
    CHECK(OledUi_InputPush(&s_oled_input_queue, OLED_UI_EVENT_NEXT, false) == SYS_OK);
    CHECK(App_OledConsumeInput() == SYS_OK);
    CHECK(s_oled_tools.view != 0U && s_oled_tools.aux_cursor == 0U);
    CHECK(g_oled_ui_dropped_inputs == 3U);

    CHECK(OledUi_InputPush(&s_oled_input_queue, OLED_UI_EVENT_BACK, false) == SYS_OK);
    CHECK(OledUi_InputPush(&s_oled_input_queue, OLED_UI_EVENT_PREVIOUS, false) == SYS_OK);
    CHECK(App_OledConsumeInput() == SYS_OK);
    CHECK(s_oled_tools.view == 0U && s_oled_tools.selected == 12U);
    CHECK(g_oled_ui_dropped_inputs == 4U && s_page_state.current == OLED_UI_PAGE_TOOLS);
    return 0U;
}

//通过实际编辑事件提交设置，再经应用刷新验证主页和两级菜单。
static uint32_t UiGlobalStyleCase(void)
{
    OledUiMenuStyle_t style;
    OledUiAnim_t anim = {0};
    OledUiVisual_t visual;
    uint8_t baseline[OLED_BUFFER_SIZE];
    uint32_t i;

    DefaultView();
    CHECK(Init() == 0U);
    CHECK(OledUi_PageOpen(&s_page_state, OLED_UI_PAGE_SYSTEM) == SYS_OK);
    CHECK(OledUi_PageOpen(&s_page_state, OLED_UI_PAGE_TOOLS) == SYS_OK);
    Ui2Post(OLED_UI_EVENT_SELECT);
    Ui2Post(OLED_UI_EVENT_NEXT);
    OledUi_ToolsReadStyle(&s_oled_tools, &style);
    CHECK(!style.tiles);
    Ui2Post(OLED_UI_EVENT_SELECT);
    Ui2Post(OLED_UI_EVENT_NEXT);
    Ui2Post(OLED_UI_EVENT_SELECT);
    Ui2Post(OLED_UI_EVENT_NEXT);
    Ui2Post(OLED_UI_EVENT_SELECT);
    Ui2Post(OLED_UI_EVENT_NEXT);
    Ui2Post(OLED_UI_EVENT_SELECT);
    Ui2Post(OLED_UI_EVENT_NEXT);
    Ui2Post(OLED_UI_EVENT_SELECT);
    Ui2Post(OLED_UI_EVENT_NEXT);
    Ui2Post(OLED_UI_EVENT_SELECT);
    Ui2Post(OLED_UI_EVENT_NEXT);
    Ui2Post(OLED_UI_EVENT_SELECT);
    OledUi_ToolsReadStyle(&s_oled_tools, &style);
    CHECK(style.tiles && style.cursor_style == 2U && style.curve == 1U && style.font_height == 12U);

    Ui2Post(OLED_UI_EVENT_BACK);
    CHECK(s_page_state.current == OLED_UI_PAGE_SYSTEM);
    Step(50U);
    CHECK(s_tx_count == 1U && Ui1_Pixel(&s_oled, 2U, 8U) && Ui1_Pixel(&s_oled, 2U, 31U));
    for (i = 0U; i < 8U; i++)
        Complete(SYS_OK);

    Ui2Post(OLED_UI_EVENT_SELECT);
    CHECK(s_page_state.current == OLED_UI_PAGE_CAMERA);
    Step(100U);
    CHECK(s_tx_count == 9U && Ui1_Pixel(&s_oled, 2U, 8U) && Ui1_Pixel(&s_oled, 2U, 31U));
    OledUi_AnimSample(&anim, &s_page_state, 100U, &style, &visual);
    CHECK(OledUi_PageMoveNext(&s_page_state) == SYS_OK);
    OledUi_AnimSample(&anim, &s_page_state, 110U, &style, &visual);
    OledUi_AnimSample(&anim, &s_page_state, 110U + style.duration_ms / 2U, &style, &visual);
    CHECK(visual.highlight_pos == 33U);
    for (i = 0U; i < 8U; i++)
        Complete(SYS_OK);

    CHECK(OledUi_PageBack(&s_page_state) == SYS_OK);
    CHECK(OledUi_PageOpen(&s_page_state, OLED_UI_PAGE_TOOLS) == SYS_OK);
    for (i = 0U; i < 4U; i++)
        Ui2Post(OLED_UI_EVENT_NEXT);
    Ui2Post(OLED_UI_EVENT_SELECT);
    Ui2Post(OLED_UI_EVENT_NEXT);
    Ui2Post(OLED_UI_EVENT_SELECT);
    Ui2Post(OLED_UI_EVENT_BACK);
    Ui2Post(OLED_UI_EVENT_BACK);
    CHECK(s_page_state.current == OLED_UI_PAGE_MONITOR);
    s_view.application.dropped_frames = UINT32_MAX;
    CHECK(OledUi_Render(&s_oled, &s_view) == SYS_OK);
    memcpy(baseline, s_oled.framebuffer, sizeof(baseline));
    Step(200U);
    CHECK(s_tx_count == 17U);
    //叠加区以外的首页像素只有明暗变化，字号及内容相同。
    for (i = 0U; i < OLED_BUFFER_SIZE; i++)
        if (i < 2U * OLED_WIDTH + 96U || i >= 3U * OLED_WIDTH)
            CHECK((uint8_t)(baseline[i] ^ s_oled.framebuffer[i]) == 0xffU);
    for (i = 2U * OLED_WIDTH + 96U; i < 3U * OLED_WIDTH; i++)
        if ((uint8_t)(baseline[i] ^ s_oled.framebuffer[i]) != 0xffU)
            break;
    CHECK(i < 3U * OLED_WIDTH);
    return 0U;
}

//按实际Tiles窗口读取图标区域，与确认预览的像素签名逐项比较。
static uint32_t UiSystemIconCase(void)
{
    static const uint32_t expected[] = {0x5ab64386UL, 0x7197b35aUL, 0x862e6e36UL, 0xb59ec5dfUL,
        0x0385b0c5UL, 0x336c9112UL, 0x9327d496UL};
    OledUiMenuStyle_t style = {0};
    OledUiPageState_t state;
    OledDevice_t *oled = &s_guard.oled;
    uint32_t i, x, y, bit, byte, checksum;

    style.font_height = 8U;
    style.cursor_style = 5U;
    style.tiles = true;
    oled->base.state = DEVICE_STATE_READY;
    oled->ops = &s_test_ops;
    CHECK(OledUi_PageInit(&state) == SYS_OK);
    CHECK(OledUi_PageOpen(&state, OLED_UI_PAGE_SYSTEM) == SYS_OK);
    for (i = 0U; i < 7U; i++)
    {
        CHECK(OledUi_MenuRenderVisual(oled, &state, NULL, NULL, &style) == SYS_OK);
        checksum = 2166136261UL;
        for (y = 8U; y < 24U; y++)
            for (x = 14U + i % 3U * 42U; x < 30U + i % 3U * 42U; x += 8U)
            {
                byte = 0U;
                for (bit = 0U; bit < 8U; bit++)
                    byte = (byte << 1) | (Ui1_Pixel(oled, x + bit, y) ? 1U : 0U);
                checksum = (checksum ^ byte) * 16777619UL;
            }
        CHECK(checksum == expected[i]);
        if (i < 6U)
            CHECK(OledUi_PageMoveNext(&state) == SYS_OK);
    }
    return 0U;
}
