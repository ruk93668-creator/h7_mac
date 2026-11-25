#include "oled_ui_anim.h"
#include <stddef.h>

//菜单切换和高亮移动的持续时间，单位ms
#define OLED_UI_ANIM_MS 240U
//长标签移进一字的时间，单位ms
#define OLED_UI_MARQUEE_MS 320U
//长标签长度扫描的最大字节数
#define OLED_UI_TEXT_SCAN 64U

//计算线性、缓动或弹簧运动的千分比进度
uint16_t OledUi_AnimProgress(uint8_t curve, uint32_t elapsed, uint32_t duration)
{
    uint32_t t, u, i, segment;
    //PID风格用有界阻尼响应表，避免采样速度改变手感或状态发散。
    static const uint16_t spring[] = {0,490,900,1080,1090,1030,997,1000};
    if (duration == 0U || elapsed >= duration)
        return 1000U;
    //把非常大的时长与时间差同比缩放，避免乘法溢出和64位除法。
    while (duration > UINT32_MAX / 1000U)
    {
        elapsed >>= 1;
        duration >>= 1;
    }
    t = elapsed * 1000U / duration;
    if (t > 999U) t = 999U;
    if (curve == 1U)
    {
        u = 1000U - t;
        return (uint16_t)(1000U - u * u / 1000U);
    }
    if (curve == 2U)
    {
        segment = t * ((sizeof(spring) / sizeof(spring[0])) - 1U);
        i = segment / 1000U;
        return (uint16_t)((int32_t)spring[i] + ((int32_t)spring[i + 1U] - spring[i]) *
            (int32_t)(segment % 1000U) / 1000);
    }
    return (uint16_t)t;
}

//计算列表首项，较大的字体减少可见行数
uint8_t OledUi_MenuFirst(const OledUiCursor_t *cursor, const OledUiMenuStyle_t *style)
{
    uint8_t rows = style == NULL ? 3U : 24U / style->font_height;
    uint8_t first = cursor->first_visible;

    if (style != NULL && style->tiles)
        return (uint8_t)(cursor->selected / 3U * 3U);
    if (cursor->selected - first >= rows)
        first = (uint8_t)(cursor->selected + 1U - rows);
    return first;
}

//推进定位动画，弹簧曲线允许过冲但光标裁剪在可见窗口内
static uint8_t OledUi_AnimPosition(const OledUiAnim_t *anim, uint32_t now,
    uint16_t duration, uint8_t curve, uint8_t lower, uint8_t upper)
{
    int32_t pos = anim->target_pos;
    if (anim->moving)
        pos = anim->from_pos + (pos - anim->from_pos) *
            OledUi_AnimProgress(curve, now - anim->moved, duration) / 1000;
    if (pos < lower)
        pos = lower;
    if (pos > upper)
        pos = upper;
    return (uint8_t)pos;
}

//采样公共的页面进入、光标移动及列表滚动动画
void OledUi_AnimSample(OledUiAnim_t *anim, const OledUiPageState_t *state,
    uint32_t now, const OledUiMenuStyle_t *style, OledUiVisual_t *visual)
{
    const OledUiCursor_t *cursor = &state->cursor[state->current];
    bool tiles = style != NULL && style->tiles;
    uint8_t height = style == NULL ? 8U : style->font_height;
    uint8_t curve = style == NULL ? 0U : style->curve;
    uint16_t duration = style == NULL ? OLED_UI_ANIM_MS : style->duration_ms;
    uint8_t first = OledUi_MenuFirst(cursor, style);
    uint8_t lower = tiles ? 2U : 8U;
    uint8_t upper = tiles ? 86U : (uint8_t)(32U - height);
    uint8_t target = (uint8_t)(lower + (cursor->selected - first) * (tiles ? 42U : height));
    uint16_t progress;

    if (anim->page != state->current || anim->target_pos == 0U)
    {
        anim->page = state->current;
        anim->first = first;
        anim->from_pos = target;
        anim->target_pos = target;
        anim->scroll = 0;
        anim->moving = false;
        anim->entered = now;
        anim->entering = true;
    }
    else if (anim->target_pos != target || anim->first != first)
    {
        anim->from_pos = OledUi_AnimPosition(anim, now, duration, curve, lower, upper);
        anim->target_pos = target;
        anim->scroll = tiles || anim->first == first ? 0 :
            (anim->first < first ? (int8_t)height : -(int8_t)height);
        anim->first = first;
        anim->moved = now;
        anim->moving = true;
    }

    if (now - anim->moved >= duration)
        anim->moving = false;
    visual->highlight_pos = OledUi_AnimPosition(anim, now, duration, curve, lower, upper);
    progress = OledUi_AnimProgress(curve, now - anim->moved, duration);
    visual->scroll_y = anim->moving ?
        (int8_t)((int32_t)anim->scroll * (1000 - (int32_t)progress) / 1000) : 0;

    visual->age = now - anim->entered;
    if (visual->age >= duration)
        anim->entering = false;
    progress = OledUi_AnimProgress(curve, visual->age, duration);
    visual->transition_width = anim->entering ?
        (uint8_t)(1U + (progress < 1000U ? progress : 1000U) * 126U / 1000U) : 0U;
}

//返回给定停留时间下长标签的可见起点
const char *OledUi_AnimText(const char *text, uint32_t width, uint32_t age, uint16_t step_ms)
{
    uint32_t count = 0U, offset, excess;

    if (text == NULL)
        return NULL;
    while (count < OLED_UI_TEXT_SCAN && text[count] != '\0')
        count++;
    if (count <= width)
        return text;
    excess = count - width;
    if (step_ms == 0U)
        step_ms = OLED_UI_MARQUEE_MS;
    offset = (age / step_ms) % (excess + 4U);
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

//弹窗到达持续时间后结束显示
void OledUi_PopupTick(OledUiPopup_t *popup, uint32_t now)
{
    if (popup->active && now - popup->opened >= popup->duration)
        popup->active = false;
}

//活动弹窗消费确认或返回事件并阻止事件传至底页
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
