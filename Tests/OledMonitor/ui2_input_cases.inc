#include "oled_ui_input.h"

static uint32_t Ui2InputCase(void)
{
    OledUiInput_t input;
    OledUi_InputInit(&input, 3U, false, 0U);
    CHECK(OledUi_InputSample(&input, 2U, false, 1U) == 0U);
    CHECK(OledUi_InputSample(&input, 3U, false, 2U) == 0U);
    CHECK(OledUi_InputSample(&input, 2U, false, 3U) == 0U);
    CHECK(OledUi_InputSample(&input, 0U, false, 4U) == 0U);
    CHECK(OledUi_InputSample(&input, 1U, false, 5U) == 0U);
    CHECK(OledUi_InputSample(&input, 3U, false, 6U) == OLED_UI_EVENT_NEXT);
    CHECK(OledUi_InputSample(&input, 1U, false, 7U) == 0U);
    CHECK(OledUi_InputSample(&input, 0U, false, 8U) == 0U);
    CHECK(OledUi_InputSample(&input, 2U, false, 9U) == 0U);
    CHECK(OledUi_InputSample(&input, 3U, false, 10U) == OLED_UI_EVENT_PREVIOUS);
    CHECK(OledUi_InputSample(&input, 0U, false, 11U) == 0U && input.phase_sum == 0);

    CHECK(OledUi_InputSample(&input, 0U, true, 20U) == 0U);
    CHECK(OledUi_InputSample(&input, 0U, false, 22U) == 0U);
    CHECK(OledUi_InputSample(&input, 0U, true, 30U) == 0U);
    CHECK(OledUi_InputSample(&input, 0U, true, 50U) == 0U);
    CHECK(OledUi_InputSample(&input, 0U, false, 80U) == 0U);
    CHECK(OledUi_InputSample(&input, 0U, false, 100U) == OLED_UI_EVENT_SELECT);
    CHECK(OledUi_InputSample(&input, 0U, false, 110U) == 0U);

    OledUi_InputInit(&input, 0U, true, 200U);
    CHECK(OledUi_InputSample(&input, 0U, true, 1000U) == 0U);
    CHECK(OledUi_InputSample(&input, 0U, false, 1010U) == 0U);
    CHECK(OledUi_InputSample(&input, 0U, false, 1030U) == 0U);
    CHECK(OledUi_InputSample(&input, 0U, true, 0xfffffff0U) == 0U);
    CHECK(OledUi_InputSample(&input, 0U, true, 4U) == 0U);
    CHECK(OledUi_InputSample(&input, 0U, true, 683U) == 0U);
    CHECK(OledUi_InputSample(&input, 0U, true, 684U) == OLED_UI_EVENT_BACK);
    CHECK(OledUi_InputSample(&input, 0U, true, 800U) == 0U);
    CHECK(OledUi_InputSample(&input, 0U, false, 801U) == 0U);
    CHECK(OledUi_InputSample(&input, 0U, false, 821U) == 0U);
    return 0U;
}
