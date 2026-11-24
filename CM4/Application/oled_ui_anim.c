#include "oled_ui_anim.h"
#include <stddef.h>

#define OLED_UI_ANIM_MS 240U
#define OLED_UI_MARQUEE_MS 320U
#define OLED_UI_TEXT_SCAN 64U

//定点插值终点准确，毫秒差使用无符号减法处理回绕。
static uint32_t OledUi_AnimElapsed(uint32_t now, uint32_t start)
{
    uint32_t elapsed = now - start;
    return elapsed < OLED_UI_ANIM_MS ? elapsed : OLED_UI_ANIM_MS;
}

static uint8_t OledUi_AnimY(const OledUiAnim_t *anim, uint32_t now)
{
    int32_t delta = (int32_t)anim->target_y - anim->from_y;
    if (!anim->moving)
        return anim->target_y;
    return (uint8_t)((int32_t)anim->from_y +
        delta * (int32_t)OledUi_AnimElapsed(now, anim->moved) / (int32_t)OLED_UI_ANIM_MS);
}

void OledUi_AnimSample(OledUiAnim_t *anim, const OledUiPageState_t *state,
    uint32_t now, OledUiVisual_t *visual)
{
    const OledUiCursor_t *cursor = &state->cursor[state->current];
    uint8_t y = (uint8_t)(8U + (cursor->selected - cursor->first_visible) * 8U);
    uint32_t elapsed;

    if (anim->page != state->current || anim->target_y == 0U)
    {
        anim->page = state->current;
        anim->first = cursor->first_visible;
        anim->from_y = y;
        anim->target_y = y;
        anim->scroll = 0;
        anim->moving = false;
        anim->entered = now;
        anim->entering = true;
    }
    else if (anim->target_y != y || anim->first != cursor->first_visible)
    {
        anim->from_y = OledUi_AnimY(anim, now);
        anim->target_y = y;
        anim->scroll = anim->first == cursor->first_visible ? 0 :
            (anim->first < cursor->first_visible ? 8 : -8);
        anim->first = cursor->first_visible;
        anim->moved = now;
        anim->moving = true;
    }

    elapsed = OledUi_AnimElapsed(now, anim->moved);
    if (elapsed == OLED_UI_ANIM_MS)
        anim->moving = false;
    visual->highlight_y = OledUi_AnimY(anim, now);
    visual->scroll_y = anim->moving ? (int8_t)((int32_t)anim->scroll *
        (int32_t)(OLED_UI_ANIM_MS - elapsed) / (int32_t)OLED_UI_ANIM_MS) : 0;

    visual->age = now - anim->entered;
    if (visual->age >= OLED_UI_ANIM_MS)
        anim->entering = false;
    visual->transition_width = anim->entering ?
        (uint8_t)(1U + visual->age * 126U / OLED_UI_ANIM_MS) : 0U;
}

const char *OledUi_AnimText(const char *text, uint32_t width, uint32_t age)
{
    uint32_t count = 0U, offset, excess;

    if (text == NULL)
        return NULL;
    while (count < OLED_UI_TEXT_SCAN && text[count] != '\0')
        count++;
    if (count <= width)
        return text;
    excess = count - width;
    offset = (age / OLED_UI_MARQUEE_MS) % (excess + 4U);
    if (offset < 2U)
        return text;
    offset -= 2U;
    return text + (offset > excess ? excess : offset);
}

static void OledUi_PopupCopy(char *dest, const char *source)
{
    uint32_t i = 0U;
    if (source != NULL)
        while (i < OLED_UI_POPUP_TEXT - 1U && source[i] != '\0')
        {
            dest[i] = source[i];
            i++;
        }
    dest[i] = '\0';
}

void OledUi_PopupOpen(OledUiPopup_t *popup, const char *title, const char *body,
    bool confirm, uint32_t now, uint32_t duration)
{
    OledUi_PopupCopy(popup->title, title);
    OledUi_PopupCopy(popup->body, body);
    if (duration < 100U)
        duration = 100U;
    if (duration > 5000U)
        duration = 5000U;
    popup->opened = now;
    popup->duration = (uint16_t)duration;
    popup->confirm = confirm;
    popup->active = true;
}

void OledUi_PopupTick(OledUiPopup_t *popup, uint32_t now)
{
    if (popup->active && now - popup->opened >= popup->duration)
        popup->active = false;
}

bool OledUi_PopupEvent(OledUiPopup_t *popup, uint32_t event, uint32_t now)
{
    OledUi_PopupTick(popup, now);
    if (!popup->active)
        return false;

    //活动弹窗独占输入，关闭不会透传到下层页面。
    if (event == OLED_UI_EVENT_BACK || event == OLED_UI_EVENT_SELECT)
        popup->active = false;
    return true;
}
