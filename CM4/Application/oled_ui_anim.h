#ifndef CM4_OLED_UI_ANIM_H
#define CM4_OLED_UI_ANIM_H

#include "oled_ui_page.h"
#include <stdbool.h>

#define OLED_UI_POPUP_TEXT 17U

typedef struct
{
    uint32_t entered;
    uint32_t moved;
    uint8_t page;
    uint8_t first;
    uint8_t from_y;
    uint8_t target_y;
    int8_t scroll;
    bool entering;
    bool moving;
} OledUiAnim_t;

typedef struct
{
    uint8_t highlight_y;
    int8_t scroll_y;
    uint8_t transition_width;
    uint32_t age;
} OledUiVisual_t;

typedef struct
{
    uint32_t opened;
    uint16_t duration;
    bool active;
    bool confirm;
    char title[OLED_UI_POPUP_TEXT];
    char body[OLED_UI_POPUP_TEXT];
} OledUiPopup_t;

//动画状态须零初始化。采样只修改视觉状态，不回写页面逻辑。
void OledUi_AnimSample(OledUiAnim_t *anim, const OledUiPageState_t *state,
    uint32_t now, OledUiVisual_t *visual);
const char *OledUi_AnimText(const char *text, uint32_t width, uint32_t age);

//弹窗只存受限文本副本，时长限制在100到5000毫秒。
void OledUi_PopupOpen(OledUiPopup_t *popup, const char *title, const char *body,
    bool confirm, uint32_t now, uint32_t duration);
void OledUi_PopupTick(OledUiPopup_t *popup, uint32_t now);
bool OledUi_PopupEvent(OledUiPopup_t *popup, uint32_t event, uint32_t now);

#endif
