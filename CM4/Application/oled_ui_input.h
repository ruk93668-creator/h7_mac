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

#define OLED_UI_INPUT_QUEUE_CAPACITY 8U
typedef struct
{
    uint8_t pending[OLED_UI_INPUT_QUEUE_CAPACITY];
    uint8_t count;
    bool reverse;
    uint32_t dropped;
} OledUiInputQueue_t;

//仅主循环使用，入队保序。动作在满队列时可挤掉最早的转动项。
SysResult_t OledUi_InputPush(OledUiInputQueue_t *queue, uint32_t event, bool physical);
uint32_t OledUi_InputTake(OledUiInputQueue_t *queue);
void OledUi_InputDiscard(OledUiInputQueue_t *queue);

#endif
