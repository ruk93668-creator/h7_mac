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

typedef enum
{
    OLED_UI_COMPAT_PARENT = 0,
    OLED_UI_COMPAT_HANDLED,
    OLED_UI_COMPAT_CONTEXT_CHANGED
} OledUiCompatResult_t;

//零初始化。窗口或子视图改变时，调用者清除已排队的旧上下文事件。
OledUiCompatResult_t OledUi_CompatEvent(OledUiCompat_t *ui, uint32_t event, uint32_t now);
SysResult_t OledUi_CompatRender(OledDevice_t *oled, const OledUiCompat_t *ui,
    uint32_t now, uint32_t fps_tenths);
SysResult_t OledUi_CompatText(OledDevice_t *oled, int16_t x, int16_t y,
    int16_t clip_left, int16_t clip_right, const char *text, size_t length, uint8_t height);
uint16_t OledUi_CompatProgress(uint8_t curve, uint32_t elapsed, uint32_t duration);

#endif
