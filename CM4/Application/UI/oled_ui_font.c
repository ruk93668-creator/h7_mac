#include "oled_ui_font.h"
#include "oled_ui_draw.h"
#include "Assets/oled_ui_assets.inc"

//计算固定大小的字模表容量
#define FONT_COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))
//保留绘图操作的错误码并立即停止写入
#define FONT_TRY(expr) do { SysResult_t result_ = (expr); if (result_ != SYS_OK) return result_; } while (0)

//获取对应字高的ASCII字模步长，单位像素
uint8_t OledUi_FontWidth(uint8_t height)
{
    return height == 8U ? 6U : height == 12U ? 7U : height == 16U ? 8U : 10U;
}

//按编码查字体表，未收录字符使用问号
static const uint8_t *OledUi_FontGlyph(uint16_t code, uint8_t height, uint8_t *width)
{
    uint32_t i;
    if (code > 0xffU)
    {
        //字库只覆盖参考项目收录的GB2312字符，未收录时安全显示问号。
#define FIND_HAN(a) for (i = 0; i < FONT_COUNT_OF(a); i++) \
    if ((a)[i].code == code) { *width = height; return (a)[i].data; }
        if (height == 8U) { FIND_HAN(s_chinese_8) }
        else if (height == 12U) { FIND_HAN(s_chinese_12) }
        else if (height == 16U) { FIND_HAN(s_chinese_16) }
        else { FIND_HAN(s_chinese_20) }
#undef FIND_HAN
    }
    i = code >= 32U && code <= 126U ? code - 32U : (uint32_t)'?' - 32U;
    *width = OledUi_FontWidth(height);
    if (height == 8U) return s_ascii_8[i];
    if (height == 12U) return s_ascii_12[i];
    if (height == 16U) return s_ascii_16[i];
    return s_ascii_20[i];
}

//在给定半开裁剪框内绘制有界字符串
SysResult_t OledUi_FontTextClip(OledDevice_t *oled, int16_t x, int16_t y,
    int16_t clip_left, int16_t clip_right, int16_t clip_top, int16_t clip_bottom,
    const char *text, size_t length, uint8_t height)
{
    size_t i;
    uint32_t row, col;
    uint8_t width;
    uint16_t code;
    int32_t left = x, px, py;
    const uint8_t *glyph;

    if (text == NULL || clip_left < 0 || clip_right > (int16_t)OLED_WIDTH ||
        clip_left > clip_right || clip_top < 0 || clip_bottom > (int16_t)OLED_HEIGHT ||
        clip_top > clip_bottom || (height != 8U && height != 12U && height != 16U && height != 20U))
        return SYS_ERR_INVALID_ARG;
    FONT_TRY(OledUi_CheckWritable(oled));

    //字模按列和8像素页排列，只写亮点并同时裁剪窗口与屏幕。
    for (i = 0U; i < length && left < clip_right && text[i] != '\0'; i++)
    {
        code = (uint8_t)text[i];
        if (code >= 0xa1U && code <= 0xf7U && i + 1U < length &&
            (uint8_t)text[i + 1U] >= 0xa1U && (uint8_t)text[i + 1U] <= 0xfeU)
            code = (uint16_t)((code << 8) | (uint8_t)text[++i]);

        glyph = OledUi_FontGlyph(code, height, &width);
        for (col = 0U; col < width; col++)
        {
            px = left + (int32_t)col;
            if (px < clip_left || px >= clip_right)
                continue;
            for (row = 0U; row < height; row++)
            {
                py = (int32_t)y + (int32_t)row;
                if (py >= clip_top && py < clip_bottom &&
                    (glyph[row / 8U * width + col] & (1U << (row % 8U))) != 0U)
                    FONT_TRY(oled->ops->draw_pixel(oled, (uint16_t)px, (uint16_t)py, true));
            }
        }
        left += width;
    }
    return SYS_OK;
}

//使用全屏纵向边界绘制多尺寸文本
SysResult_t OledUi_FontText(OledDevice_t *oled, int16_t x, int16_t y,
    int16_t clip_left, int16_t clip_right, const char *text, size_t length, uint8_t height)
{
    return OledUi_FontTextClip(oled, x, y, clip_left, clip_right, 0, OLED_HEIGHT,
        text, length, height);
}
