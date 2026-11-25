#include "oled_ui_tools.h"
#include "oled_ui_font.h"

uint8_t g_compat_frames[36][OLED_BUFFER_SIZE];
uint32_t g_compat_count;

static SysResult_t UiCompatCapture(OledDevice_t *oled, OledUiTools_t *ui, uint32_t now)
{
    SysResult_t result = OledUi_ToolsRender(oled, ui, now, 200U);
    if (result == SYS_OK && g_compat_count < 36U)
    {
        memcpy(g_compat_frames[g_compat_count], oled->framebuffer, OLED_BUFFER_SIZE);
        g_compat_count++;
    }
    return result;
}

static uint32_t UiCompatCase(uint32_t id)
{
    OledUiTools_t ui = {0};
    OledDevice_t *oled = &s_guard.oled;
    uint32_t i;
    static const uint8_t heights[] = {8U, 12U, 16U, 20U};

    oled->base.state = DEVICE_STATE_READY;
    oled->ops = &s_test_ops;
    if (id == 86U)
    {
        CHECK(OledUi_AnimProgress(0, 50, 100) == 500);
        CHECK(OledUi_AnimProgress(1, 50, 100) == 750);
        CHECK(OledUi_AnimProgress(2, 50, 100) > 1000);
        CHECK(OledUi_AnimProgress(2, 100, 100) == 1000);
        for (i = 0U; i < 4U; i++)
        {
            uint32_t before = s_pixels;
            CHECK(Oled_ClearImpl(oled) == SYS_OK);
            CHECK(OledUi_FontText(oled, -1, 1, 0, 7, "A!", 2, heights[i]) == SYS_OK);
            CHECK(s_pixels > before);
            CHECK(oled->framebuffer[7] == 0U && oled->framebuffer[128+7] == 0U);
        }
        ui.selected = 5U;
        CHECK(OledUi_ToolsEvent(&ui, OLED_UI_EVENT_SELECT, 0U));
        for (i=0U;i<40U;i++) CHECK(OledUi_ToolsEvent(&ui, OLED_UI_EVENT_NEXT, i+1U));
        CHECK(ui.draft == 600 && ui.values[5] == 0);
        CHECK(OledUi_ToolsEvent(&ui, OLED_UI_EVENT_BACK, 50U) && ui.values[5] == 0);
        CHECK(OledUi_ToolsEvent(&ui, OLED_UI_EVENT_SELECT, 60U));
        CHECK(OledUi_ToolsEvent(&ui, OLED_UI_EVENT_NEXT, 70U));
        CHECK(OledUi_ToolsEvent(&ui, OLED_UI_EVENT_SELECT, 80U) && ui.values[5] == 20);
        ui.selected = 6U;
        CHECK(OledUi_ToolsEvent(&ui, OLED_UI_EVENT_SELECT, 90U));
        CHECK(OledUi_ToolsEvent(&ui, OLED_UI_EVENT_NEXT, 100U) && ui.draft == 25);
        CHECK(OledUi_ToolsEvent(&ui, OLED_UI_EVENT_SELECT, 110U) && ui.values[6] == 25);
        ui.selected = 7U;
        CHECK(OledUi_ToolsEvent(&ui, OLED_UI_EVENT_SELECT, 120U) && ui.values[7] == 1);
        oled->refresh_state = OLED_REFRESH_WAIT_DATA;
        CHECK(OledUi_ToolsRender(oled, &ui, 130U, 200U) == SYS_ERR_BUSY);
        return 0U;
    }

    ui.values[5] = 180;
    CHECK(UiCompatCapture(oled, &ui, 0U) == SYS_OK);
    CHECK(OledUi_ToolsEvent(&ui, OLED_UI_EVENT_NEXT, 100U));
    for (i = 0U; i <= 4U; i++)
        CHECK(UiCompatCapture(oled, &ui, 100U + i * 75U) == SYS_OK);
    CHECK(memcmp(g_compat_frames[1], g_compat_frames[3], OLED_BUFFER_SIZE) != 0);
    for (i = 0U; i < 6U; i++)
    {
        ui.values[1] = (int32_t)i;
        CHECK(UiCompatCapture(oled, &ui, 450U) == SYS_OK);
        if (i != 0U)
            CHECK(memcmp(g_compat_frames[5U+i], g_compat_frames[6U+i], OLED_BUFFER_SIZE) != 0);
    }
    ui.values[1] = 0;
    ui.values[0] = 1;
    CHECK(UiCompatCapture(oled, &ui, 500U) == SYS_OK);
    CHECK(OledUi_ToolsEvent(&ui, OLED_UI_EVENT_NEXT, 550U));
    CHECK(UiCompatCapture(oled, &ui, 650U) == SYS_OK);
    CHECK(UiCompatCapture(oled, &ui, 900U) == SYS_OK);

    ui.values[0] = 0;
    ui.values[3] = 3;
    ui.entered = 1000U;
    CHECK(UiCompatCapture(oled, &ui, 1000U) == SYS_OK);
    CHECK(UiCompatCapture(oled, &ui, 2000U) == SYS_OK);
    CHECK(UiCompatCapture(oled, &ui, 2700U) == SYS_OK);
    CHECK(memcmp(g_compat_frames[15], g_compat_frames[17], OLED_BUFFER_SIZE) != 0);
    ui.values[3] = 0;
    ui.values[4] = 1;
    ui.selected = 6U;
    CHECK(OledUi_ToolsEvent(&ui, OLED_UI_EVENT_SELECT, 3000U));
    for (i = 0; i < 4U; i++)
        CHECK(UiCompatCapture(oled, &ui, 3000U + 70U * i) == SYS_OK);
    CHECK(OledUi_ToolsEvent(&ui, OLED_UI_EVENT_NEXT, 3250U));
    CHECK(UiCompatCapture(oled, &ui, 3250U) == SYS_OK);
    CHECK(OledUi_ToolsEvent(&ui, OLED_UI_EVENT_SELECT, 3300U));
    CHECK(UiCompatCapture(oled, &ui, 3300U) == SYS_OK);

    ui.selected = 10U;
    CHECK(OledUi_ToolsEvent(&ui, OLED_UI_EVENT_SELECT, 3400U));
    CHECK(UiCompatCapture(oled, &ui, 3400U) == SYS_OK);
    CHECK(UiCompatCapture(oled, &ui, 3560U) == SYS_OK);
    CHECK(memcmp(g_compat_frames[24], g_compat_frames[25], OLED_BUFFER_SIZE) != 0);
    CHECK(OledUi_ToolsEvent(&ui, OLED_UI_EVENT_BACK, 3600U));
    ui.selected = 11U;
    CHECK(OledUi_ToolsEvent(&ui, OLED_UI_EVENT_SELECT, 3650U));
    CHECK(UiCompatCapture(oled, &ui, 3650U) == SYS_OK);
    for (i=0;i<OLED_BUFFER_SIZE;i++) CHECK(oled->framebuffer[i] == 0U);
    CHECK(OledUi_ToolsEvent(&ui, OLED_UI_EVENT_BACK, 3700U));
    ui.selected = 13U;
    CHECK(OledUi_ToolsEvent(&ui, OLED_UI_EVENT_SELECT, 3900U));
    CHECK(UiCompatCapture(oled, &ui, 4080U) == SYS_OK);
    CHECK(Ui1_Pixel(oled, 64U, 1U) && !Ui1_Pixel(oled, 64U, 13U));
    CHECK(OledUi_ToolsEvent(&ui, OLED_UI_EVENT_BACK, 4100U));
    ui.selected = 12U;
    CHECK(OledUi_ToolsEvent(&ui, OLED_UI_EVENT_SELECT, 4200U));
    CHECK(UiCompatCapture(oled, &ui, 4400U) == SYS_OK);
    CHECK(!Ui1_Pixel(oled, 15U, 15U) && Ui1_Pixel(oled,16U,15U));
    CHECK(OledUi_ToolsEvent(&ui, OLED_UI_EVENT_NEXT, 4450U));
    CHECK(OledUi_ToolsEvent(&ui, OLED_UI_EVENT_NEXT, 4500U));
    CHECK(UiCompatCapture(oled, &ui, 4600U) == SYS_OK);
    CHECK(memcmp(g_compat_frames[28],g_compat_frames[29],OLED_BUFFER_SIZE) != 0);
    CHECK(OledUi_ToolsEvent(&ui, OLED_UI_EVENT_BACK, 4800U));
    ui.selected = 14U;
    CHECK(OledUi_ToolsEvent(&ui, OLED_UI_EVENT_SELECT, 5000U));
    CHECK(UiCompatCapture(oled, &ui, 5200U) == SYS_OK);
    CHECK(Ui1_Pixel(oled,14U,11U) && Ui1_Pixel(oled,44U,12U));
    CHECK(OledUi_ToolsEvent(&ui, OLED_UI_EVENT_BACK, 5300U));
    ui.selected = 15U;
    CHECK(OledUi_ToolsEvent(&ui, OLED_UI_EVENT_SELECT, 5500U));
    CHECK(UiCompatCapture(oled, &ui, 5700U) == SYS_OK);
    CHECK(OledUi_ToolsEvent(&ui, OLED_UI_EVENT_BACK, 5800U));
    ui.selected = 5U;
    CHECK(OledUi_ToolsEvent(&ui, OLED_UI_EVENT_SELECT, 5900U));
    CHECK(UiCompatCapture(oled, &ui, 6100U) == SYS_OK);
    CHECK(OledUi_ToolsEvent(&ui, OLED_UI_EVENT_NEXT, 6150U));
    CHECK(UiCompatCapture(oled, &ui, 6200U) == SYS_OK);
    CHECK(OledUi_ToolsEvent(&ui, OLED_UI_EVENT_SELECT, 6250U));
    ui.selected = 7U;
    CHECK(UiCompatCapture(oled, &ui, 6400U) == SYS_OK);
    CHECK(OledUi_ToolsEvent(&ui, OLED_UI_EVENT_SELECT, 6500U));
    CHECK(UiCompatCapture(oled, &ui, 6600U) == SYS_OK);
    CHECK(memcmp(g_compat_frames[34],g_compat_frames[35],OLED_BUFFER_SIZE) != 0);
    CHECK(g_compat_count == 36U);
    return 0U;
}
