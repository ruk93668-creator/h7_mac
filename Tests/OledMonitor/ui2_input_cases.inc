#include "oled_ui_input.h"

static uint32_t Ui2InputCase(void)
{
    OledUiInput_t input;
    OledUiInputQueue_t queue = {{0}, 0U, false, 0U};
    uint32_t i;
    CHECK(OledUi_InputPush(NULL, OLED_UI_EVENT_NEXT, true) == SYS_ERR_INVALID_ARG);
    for (i = 0U; i < OLED_UI_INPUT_QUEUE_CAPACITY; i++)
        CHECK(OledUi_InputPush(&queue, OLED_UI_EVENT_NEXT, true) == SYS_OK);
    CHECK(OledUi_InputPush(&queue, OLED_UI_EVENT_PREVIOUS, true) == SYS_ERR_NO_RESOURCE);
    CHECK(OledUi_InputPush(&queue, OLED_UI_EVENT_SELECT, true) == SYS_OK);
    CHECK(OledUi_InputPush(&queue, OLED_UI_EVENT_BACK, true) == SYS_OK);
    for (i = 0U; i < OLED_UI_INPUT_QUEUE_CAPACITY - 2U; i++)
        CHECK(OledUi_InputTake(&queue) == OLED_UI_EVENT_NEXT);
    CHECK(OledUi_InputTake(&queue) == OLED_UI_EVENT_SELECT);
    CHECK(OledUi_InputTake(&queue) == OLED_UI_EVENT_BACK);
    CHECK(queue.dropped == 3U && OledUi_InputTake(&queue) == 0U);
    queue.reverse = true;
    CHECK(OledUi_InputPush(&queue, OLED_UI_EVENT_NEXT, true) == SYS_OK);
    CHECK(OledUi_InputTake(&queue) == OLED_UI_EVENT_PREVIOUS);
    CHECK(OledUi_InputPush(&queue, OLED_UI_EVENT_NEXT, false) == SYS_OK);
    CHECK(OledUi_InputTake(&queue) == OLED_UI_EVENT_NEXT);
    for (i = 0U; i < OLED_UI_INPUT_QUEUE_CAPACITY; i++)
        CHECK(OledUi_InputPush(&queue, OLED_UI_EVENT_SELECT, true) == SYS_OK);
    CHECK(OledUi_InputPush(&queue, OLED_UI_EVENT_BACK, true) == SYS_ERR_NO_RESOURCE);
    OledUi_InputDiscard(&queue);
    CHECK(queue.dropped == 12U && OledUi_InputTake(&queue) == 0U);
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
    CHECK(Init() == 0U);
    CHECK(OledUi_InputPush(&s_oled_input_queue, OLED_UI_EVENT_OPEN_MENU, false) == SYS_OK);
    CHECK(OledUi_InputPush(&s_oled_input_queue, OLED_UI_EVENT_NEXT, false) == SYS_OK);
    CHECK(App_OledConsumeInput() == SYS_OK);
    CHECK(s_ui2_state.current == OLED_UI_PAGE_SYSTEM);
    CHECK(s_ui2_state.cursor[OLED_UI_PAGE_SYSTEM].selected == 0U);
    CHECK(g_oled_ui_dropped_inputs == 1U);
    return 0U;
}
