//输入队列允许连发，但旧页旋转不能进入刚打开的窗口或局部页。
static uint32_t UiPreMergeCase(void)
{
    uint32_t i;

    CHECK(Init() == 0U);
    CHECK(OledUi_PageOpen(&s_ui2_state, OLED_UI_PAGE_SYSTEM) == SYS_OK);
    CHECK(OledUi_PageOpen(&s_ui2_state, OLED_UI_PAGE_TOOLS) == SYS_OK);
    CHECK(OledUi_InputPush(&s_oled_input_queue, OLED_UI_EVENT_SELECT, false) == SYS_OK);
    CHECK(OledUi_InputPush(&s_oled_input_queue, OLED_UI_EVENT_NEXT, false) == SYS_OK);
    CHECK(App_OledConsumeInput() == SYS_OK);
    CHECK(s_oled_compat.modal != 0U && s_oled_compat.draft == 0);
    CHECK(g_oled_ui_dropped_inputs == 1U);

    CHECK(OledUi_InputPush(&s_oled_input_queue, OLED_UI_EVENT_NEXT, false) == SYS_OK);
    CHECK(OledUi_InputPush(&s_oled_input_queue, OLED_UI_EVENT_SELECT, false) == SYS_OK);
    CHECK(OledUi_InputPush(&s_oled_input_queue, OLED_UI_EVENT_NEXT, false) == SYS_OK);
    CHECK(App_OledConsumeInput() == SYS_OK);
    CHECK(s_oled_compat.modal == 0U && s_oled_compat.values[0] == 1);
    CHECK(s_oled_compat.selected == 0U && g_oled_ui_dropped_inputs == 2U);

    for (i = 0U; i < 12U; i++)
        Ui2Post(OLED_UI_EVENT_NEXT);
    CHECK(s_oled_compat.selected == 12U);
    CHECK(OledUi_InputPush(&s_oled_input_queue, OLED_UI_EVENT_SELECT, false) == SYS_OK);
    CHECK(OledUi_InputPush(&s_oled_input_queue, OLED_UI_EVENT_NEXT, false) == SYS_OK);
    CHECK(App_OledConsumeInput() == SYS_OK);
    CHECK(s_oled_compat.view != 0U && s_oled_compat.aux_cursor == 0U);
    CHECK(g_oled_ui_dropped_inputs == 3U);

    CHECK(OledUi_InputPush(&s_oled_input_queue, OLED_UI_EVENT_BACK, false) == SYS_OK);
    CHECK(OledUi_InputPush(&s_oled_input_queue, OLED_UI_EVENT_PREVIOUS, false) == SYS_OK);
    CHECK(App_OledConsumeInput() == SYS_OK);
    CHECK(s_oled_compat.view == 0U && s_oled_compat.selected == 12U);
    CHECK(g_oled_ui_dropped_inputs == 4U && s_ui2_state.current == OLED_UI_PAGE_TOOLS);
    return 0U;
}
