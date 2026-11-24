static uint32_t Ui4Case(uint32_t id)
{
    OledUiAnim_t anim = {0};
    OledUiVisual_t visual;
    OledUiPageState_t state;
    OledUiPopup_t popup = {0};
    uint32_t start = 0xfffffff0U;
    char text[] = "TEMPORARY";
    OledDevice_t *oled = &s_guard.oled;

    CHECK(OledUi_PageInit(&state) == SYS_OK);
    CHECK(OledUi_PageOpen(&state, OLED_UI_PAGE_SYSTEM) == SYS_OK);
    if (id == 77U)
    {
        OledUi_AnimSample(&anim, &state, start, &visual);
        CHECK(visual.highlight_y == 8U && visual.transition_width == 1U);
        CHECK(OledUi_PageMoveNext(&state) == SYS_OK);
        OledUi_AnimSample(&anim, &state, start+16U, &visual);
        OledUi_AnimSample(&anim, &state, start+136U, &visual);
        CHECK(visual.highlight_y == 12U && visual.transition_width > 1U);
        OledUi_AnimSample(&anim, &state, start+256U, &visual);
        CHECK(visual.highlight_y == 16U && visual.transition_width == 0U);
        CHECK(OledUi_PageMoveNext(&state) == SYS_OK && OledUi_PageMoveNext(&state) == SYS_OK);
        OledUi_AnimSample(&anim, &state, start+300U, &visual);
        CHECK(visual.scroll_y == 8);
        OledUi_AnimSample(&anim, &state, start+420U, &visual);
        CHECK(visual.scroll_y == 4);
        OledUi_AnimSample(&anim, &state, start+540U, &visual);
        CHECK(visual.scroll_y == 0 && visual.highlight_y == 24U);
        CHECK(strcmp(OledUi_AnimText("ABCDE", 3U, 960U), "BCDE") == 0);
        CHECK(strcmp(OledUi_AnimText("ABCDE", 3U, 1600U), "CDE") == 0);
    }
    else
    {
        oled->base.state = DEVICE_STATE_READY;
        oled->ops = &s_test_ops;
        OledUi_PopupOpen(&popup, text, "TEST", true, start, 400U);
        text[0] = 'X';
        CHECK(strcmp(popup.title,"TEMPORARY") == 0);
        CHECK(OledUi_PopupEvent(&popup, OLED_UI_EVENT_NEXT, start+20U) && popup.active);
        CHECK(OledUi_MenuPopup(oled, &popup, start+150U) == SYS_OK);
        CHECK(Ui1_Pixel(oled, 8U, 2U) && !Ui1_Pixel(oled, 7U, 2U));
        CHECK(OledUi_PopupEvent(&popup, OLED_UI_EVENT_BACK, start+151U) && !popup.active);
        OledUi_PopupOpen(&popup, "INFO", "OK", true, start, 400U);
        CHECK(OledUi_PopupEvent(&popup, OLED_UI_EVENT_SELECT, start+20U) && !popup.active);
        OledUi_PopupOpen(&popup, "INFO", "OK", false, start, 1U);
        OledUi_PopupTick(&popup, start+99U);
        CHECK(popup.active);
        CHECK(!OledUi_PopupEvent(&popup, OLED_UI_EVENT_BACK, start+100U) && !popup.active);
    }
    return 0U;
}
