#include "oled_ui_input.h"
#include <stddef.h>

//按键电平必须连续稳定的时间，单位ms
#define OLED_UI_PRESS_FILTER_MS 20U
//发送长按事件所需的持续时间，单位ms
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

//转动不合并，保留每一档。动作只优先保留，不抢跑其他已入队事件。
static bool OledUi_InputIsTurn(uint32_t event)
{
    return event == OLED_UI_EVENT_NEXT || event == OLED_UI_EVENT_PREVIOUS;
}

SysResult_t OledUi_InputPush(OledUiInputQueue_t *queue, uint32_t event, bool physical)
{
    uint32_t i;

    if (queue == NULL || event < OLED_UI_EVENT_OPEN_MENU || event > OLED_UI_EVENT_BACK)
        return SYS_ERR_INVALID_ARG;
    if (queue->count > OLED_UI_INPUT_QUEUE_CAPACITY)
        return SYS_ERR_INVALID_STATE;

    if (physical && queue->reverse && OledUi_InputIsTurn(event))
        event = event == OLED_UI_EVENT_NEXT ? OLED_UI_EVENT_PREVIOUS : OLED_UI_EVENT_NEXT;

    if (queue->count == OLED_UI_INPUT_QUEUE_CAPACITY)
    {
        queue->dropped++;
        if (OledUi_InputIsTurn(event))
            return SYS_ERR_NO_RESOURCE;

        for (i = 0U; i < queue->count && !OledUi_InputIsTurn(queue->pending[i]); i++)
            ;
        if (i == queue->count)
            return SYS_ERR_NO_RESOURCE;
        for (; i + 1U < queue->count; i++)
            queue->pending[i] = queue->pending[i + 1U];
        queue->count--;
    }
    queue->pending[queue->count++] = (uint8_t)event;
    return SYS_OK;
}

//取出最早入队的事件，空队列返回零
uint32_t OledUi_InputTake(OledUiInputQueue_t *queue)
{
    uint32_t event, i;

    if (queue == NULL || queue->count == 0U || queue->count > OLED_UI_INPUT_QUEUE_CAPACITY)
        return 0U;
    event = queue->pending[0];
    for (i = 1U; i < queue->count; i++)
        queue->pending[i - 1U] = queue->pending[i];
    queue->count--;
    return event;
}

//切页后清除旧上下文事件并累计丢弃数量
void OledUi_InputDiscard(OledUiInputQueue_t *queue)
{
    if (queue != NULL)
    {
        queue->dropped += queue->count;
        queue->count = 0U;
    }
}
