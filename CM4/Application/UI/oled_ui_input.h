#ifndef CM4_OLED_UI_INPUT_H
#define CM4_OLED_UI_INPUT_H

#include <stdbool.h>
#include "oled_ui_page.h"

typedef struct
{
    uint32_t changed_at; //候选电平变化时间，单位ms
    uint32_t pressed_at; //稳定按下起点，单位ms
    int8_t phase_sum; //本次刻度相位累计
    uint8_t phase; //上一组采样的相位
    bool candidate; //候选按压状态
    bool stable; //去抖后的按压状态
    bool armed; //已释放并可识别后续按压
    bool long_sent; //本次按压已发送长按事件
} OledUiInput_t;

//接收已采样的两相与按压。无事件返回0，不接触GPIO或显示设备。
void OledUi_InputInit(OledUiInput_t *input, uint8_t phase, bool pressed, uint32_t now);
uint32_t OledUi_InputSample(OledUiInput_t *input, uint8_t phase, bool pressed, uint32_t now);

//同一主循环中最多暂存的输入事件数
#define OLED_UI_INPUT_QUEUE_CAPACITY 8U
typedef struct
{
    uint8_t pending[OLED_UI_INPUT_QUEUE_CAPACITY]; //从旧到新暂存的事件
    uint8_t count; //有效事件数
    bool reverse; //交换物理转向
    uint32_t dropped; //累计丢弃事件数
} OledUiInputQueue_t;

//仅主循环使用，入队保序。动作在满队列时可挤掉最早的转动项。
SysResult_t OledUi_InputPush(OledUiInputQueue_t *queue, uint32_t event, bool physical);
//取出最早入队的事件，空队列返回零
uint32_t OledUi_InputTake(OledUiInputQueue_t *queue);
//切页后清除旧上下文事件并累计丢弃数量
void OledUi_InputDiscard(OledUiInputQueue_t *queue);

#endif
