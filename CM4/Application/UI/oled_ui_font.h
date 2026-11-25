#ifndef CM4_OLED_UI_FONT_H
#define CM4_OLED_UI_FONT_H

#include "oled_device.h"
#include <stddef.h>

//返回指定字高对应的ASCII字模像素宽度
uint8_t OledUi_FontWidth(uint8_t height);

//在半开矩形内绘制字模；调用方提供有效屏幕裁剪边界
SysResult_t OledUi_FontTextClip(OledDevice_t *oled, int16_t x, int16_t y,
    int16_t clip_left, int16_t clip_right, int16_t clip_top, int16_t clip_bottom,
    const char *text, size_t length, uint8_t height);

//限制横向绘制范围，纵向按全屏范围裁剪
SysResult_t OledUi_FontText(OledDevice_t *oled, int16_t x, int16_t y,
    int16_t clip_left, int16_t clip_right, const char *text, size_t length, uint8_t height);

#endif
