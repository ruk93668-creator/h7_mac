#ifndef CM4_OLED_UI_COMPAT_H
#define CM4_OLED_UI_COMPAT_H

#include "oled_device.h"
#include "oled_ui_page.h"
#include <stddef.h>

typedef struct
{
    int32_t values[9];
    int32_t draft;
    uint32_t opened, entered, moved, page_at;
    int16_t from;
    uint8_t selected, view, modal, aux_cursor;
} OledUiCompat_t;

//零初始化。事件只更新本地参数；返回false表示请交给父页面处理。
bool OledUi_CompatEvent(OledUiCompat_t *ui, uint32_t event, uint32_t now);
SysResult_t OledUi_CompatRender(OledDevice_t *oled, const OledUiCompat_t *ui,
    uint32_t now, uint32_t fps_tenths);
SysResult_t OledUi_CompatText(OledDevice_t *oled, int16_t x, int16_t y,
    int16_t clip_left, int16_t clip_right, const char *text, size_t length, uint8_t height);
uint16_t OledUi_CompatProgress(uint8_t curve, uint32_t elapsed, uint32_t duration);

#endif
