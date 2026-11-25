//OLED图形只在CM4应用主循环的IDLE阶段写入设备Framebuffer。
#ifndef CM4_OLED_UI_DRAW_H
#define CM4_OLED_UI_DRAW_H

#include <stddef.h>
#include "oled_device.h"

//公共契约：空设备或无效输入返回INVALID_ARG，非IDLE返回BUSY，
//未READY返回INVALID_STATE，缺少所需Ops返回NOT_SUPPORTED。
//有效的零尺寸或屏外图元返回OK且不写入。像素Ops失败时立即返回原错误，
//已写像素由调用者按需要回滚。接口不可从中断调用，不提交刷新。
SysResult_t OledUi_DrawLine(OledDevice_t *oled,
    int16_t x0, int16_t y0, int16_t x1, int16_t y1, bool on);
//绘制矩形外边缘
SysResult_t OledUi_DrawRect(OledDevice_t *oled,
    int16_t x, int16_t y, uint16_t width, uint16_t height, bool on);
//用指定像素值覆盖矩形内部
SysResult_t OledUi_FillRect(OledDevice_t *oled,
    int16_t x, int16_t y, uint16_t width, uint16_t height, bool on);
//圆角半径限制为较短边减一的一半；半径零等同普通边框。
SysResult_t OledUi_DrawRoundRect(OledDevice_t *oled,
    int16_t x, int16_t y, uint16_t width, uint16_t height, uint16_t radius, bool on);
//按原像素值翻转裁剪区域
SysResult_t OledUi_InvertArea(OledDevice_t *oled,
    int16_t x, int16_t y, uint16_t width, uint16_t height);
//反色填满圆角矩形，圆弧外像素保持原值
SysResult_t OledUi_InvertRoundRect(OledDevice_t *oled,
    int16_t x, int16_t y, uint16_t width, uint16_t height, uint16_t radius);

//绘制16×16单色图标，输入为32字节，缩放后按最近像素采样
SysResult_t OledUi_DrawIcon16(OledDevice_t *oled, int16_t x, int16_t y,
    uint8_t size, const uint8_t bits[32]);

//每行独立按字节对齐、MSB在左。0清除、1点亮。length为可读字节数。
SysResult_t OledUi_DrawBitmap(OledDevice_t *oled,
    int16_t x, int16_t y, uint16_t width, uint16_t height,
    const uint8_t *data, size_t length);
//每格6x8，含清零背景。不可打印字节显示为问号。
SysResult_t OledUi_DrawChar(OledDevice_t *oled, int16_t x, int16_t y, uint8_t character);
//最多读取length字节，遇NUL提前停止。输入数据不得与Framebuffer重叠。
SysResult_t OledUi_DrawString(OledDevice_t *oled,
    int16_t x, int16_t y, const char *text, size_t length);

//供UI文字与图形实现共用的写权限检查。
SysResult_t OledUi_CheckWritable(const OledDevice_t *oled);

#endif
