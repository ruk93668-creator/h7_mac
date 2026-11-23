#include "oled_ui_input.h"

#define OLED_UI_PRESS_FILTER_MS 20U
#define OLED_UI_LONG_PRESS_MS 700U

void OledUi_InputInit(OledUiInput_t *input, uint8_t phase, bool pressed, uint32_t now)
{
    input->changed_at = now;
    input->pressed_at = now;
    input->phase_sum = 0;
    input->phase = phase & 3U;
    input->candidate = pressed;
    input->stable = pressed;
    input->armed = !pressed;
    input->long_sent = false;
}

uint32_t OledUi_InputSample(OledUiInput_t *input, uint8_t phase, bool pressed, uint32_t now)
{
    //四个相邻的合法相位才完成一步，跳过对角状态时丢弃半步。
    static const int8_t s_delta[16] =
    {
         0,  1, -1,  0,
        -1,  0,  0,  1,
         1,  0,  0, -1,
         0, -1,  1,  0
    };
    uint32_t turn = 0U;

    phase &= 3U;
    if ((input->phase ^ phase) == 3U)
        input->phase_sum = 0;
    else
        input->phase_sum += s_delta[(input->phase << 2) | phase];

    input->phase = phase;
    if (input->phase_sum == 4 || input->phase_sum == -4)
    {
        turn = input->phase_sum > 0 ? OLED_UI_EVENT_NEXT : OLED_UI_EVENT_PREVIOUS;
        input->phase_sum = 0;
    }

    if (pressed != input->candidate)
    {
        input->candidate = pressed;
        input->changed_at = now;
    }
    if (input->candidate != input->stable && now - input->changed_at >= OLED_UI_PRESS_FILTER_MS)
    {
        input->stable = input->candidate;
        if (input->stable)
        {
            input->pressed_at = input->changed_at;
            input->long_sent = false;
        }
        else
        {
            if (!input->armed)
                input->armed = true;
            else if (!input->long_sent)
                return input->changed_at - input->pressed_at >= OLED_UI_LONG_PRESS_MS ?
                    OLED_UI_EVENT_BACK : OLED_UI_EVENT_SELECT;
        }
    }
    if (input->armed && input->stable && input->candidate && !input->long_sent &&
        now - input->pressed_at >= OLED_UI_LONG_PRESS_MS)
    {
        input->long_sent = true;
        return OLED_UI_EVENT_BACK;
    }
    //同一采样出现按键事件和转动时，按键决定界面上下文。
    return turn;
}
