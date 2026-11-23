#ifndef CM4_OLED_UI_INPUT_H
#define CM4_OLED_UI_INPUT_H

#include <stdbool.h>
#include "oled_ui_page.h"

typedef struct
{
    uint32_t changed_at;
    uint32_t pressed_at;
    int8_t phase_sum;
    uint8_t phase;
    bool candidate;
    bool stable;
    bool armed;
    bool long_sent;
} OledUiInput_t;

//接收已采样的两相与按压。无事件返回0，不接触GPIO或显示设备。
void OledUi_InputInit(OledUiInput_t *input, uint8_t phase, bool pressed, uint32_t now);
uint32_t OledUi_InputSample(OledUiInput_t *input, uint8_t phase, bool pressed, uint32_t now);

#endif
