//在应用层裁剪图元，仅通过Device像素接口修改现有Framebuffer。
#include "oled_ui_draw.h"

typedef enum
{
    OLED_UI_OUTLINE,
    OLED_UI_FILL,
    OLED_UI_INVERT,
    OLED_UI_BITMAP
} OledUi_DrawMode_t;

//只有应用主循环能持有写权限，异步刷新会在后续轮询中读取各页。
SysResult_t OledUi_CheckWritable(const OledDevice_t *oled)
{
    if (oled == NULL)
        return SYS_ERR_INVALID_ARG;
    if (oled->refresh_state != OLED_REFRESH_IDLE)
        return SYS_ERR_BUSY;
    if (oled->base.state != DEVICE_STATE_READY)
        return SYS_ERR_INVALID_STATE;
    if (oled->ops == NULL || oled->ops->draw_pixel == NULL)
        return SYS_ERR_NOT_SUPPORTED;
    return SYS_OK;
}

//沿主轴只遍历可见区间，保留原线段的取整相位和两端。
SysResult_t OledUi_DrawLine(OledDevice_t *oled,
    int16_t x0, int16_t y0, int16_t x1, int16_t y1, bool on)
{
    int32_t dx = (int32_t)x1 - x0;
    int32_t dy = (int32_t)y1 - y0;
    bool steep = (dy < 0 ? -dy : dy) > (dx < 0 ? -dx : dx);
    int32_t major0 = steep ? y0 : x0;
    int32_t major1 = steep ? y1 : x1;
    int32_t minor0 = steep ? x0 : y0;
    int32_t minor1 = steep ? x1 : y1;
    int32_t limit = steep ? OLED_HEIGHT : OLED_WIDTH;
    int32_t minor_limit = steep ? OLED_WIDTH : OLED_HEIGHT;
    int32_t major, minor, delta, sign, span;
    SysResult_t result = OledUi_CheckWritable(oled);

    if (result != SYS_OK)
        return result;

    //统一端点顺序，反向绘制会命中同一组像素。
    if (major0 > major1)
    {
        major = major0;
        major0 = major1;
        major1 = major;
        minor = minor0;
        minor0 = minor1;
        minor1 = minor;
    }

    span = major1 - major0;
    delta = minor1 - minor0;
    sign = delta < 0 ? -1 : 1;
    if (delta < 0)
        delta = -delta;

    for (major = major0 < 0 ? 0 : major0; major <= major1 && major < limit; major++)
    {
        //16位端点差的乘积加半个主轴仍在uint32_t范围内。
        minor = minor0;
        if (span != 0)
            minor += sign * (int32_t)(((uint32_t)(major - major0) * (uint32_t)delta +
                (uint32_t)span / 2U) / (uint32_t)span);
        if (minor < 0 || minor >= minor_limit)
            continue;

        result = oled->ops->draw_pixel(oled, (uint16_t)(steep ? minor : major),
            (uint16_t)(steep ? major : minor), on);
        if (result != SYS_OK)
            return result;
    }
    return SYS_OK;
}

//检测像素是否落入圆角矩形；角部平方和在32位无符号范围内。
static bool OledUi_InsideRoundRect(int32_t x, int32_t y,
    int32_t width, int32_t height, int32_t radius)
{
    int32_t dx, dy;

    if (x < 0 || x >= width || y < 0 || y >= height)
        return false;
    if (x >= width / 2)
        x = width - 1 - x;
    if (y >= height / 2)
        y = height - 1 - y;

    dx = radius - x;
    dy = radius - y;
    if (dx <= 0 || dy <= 0)
        return true;

    return (uint32_t)dx * (uint32_t)dx + (uint32_t)dy * (uint32_t)dy <=
        (uint32_t)radius * (uint32_t)radius;
}

//用原图元位置判定边框，裁剪边不会生成新的边框像素。
static SysResult_t OledUi_DrawArea(OledDevice_t *oled,
    int16_t x, int16_t y, uint16_t width, uint16_t height, uint16_t radius,
    OledUi_DrawMode_t mode, bool on, const uint8_t *data, size_t length)
{
    int32_t right = (int32_t)x + width;
    int32_t bottom = (int32_t)y + height;
    int32_t px, py, sx, sy;
    uint32_t stride = ((uint32_t)width + 7U) / 8U;
    uint16_t short_side = width < height ? width : height;
    SysResult_t result;

    if (mode == OLED_UI_BITMAP &&
        (data == NULL || (stride != 0U && height > length / stride)))
        return SYS_ERR_INVALID_ARG;

    result = OledUi_CheckWritable(oled);
    if (result != SYS_OK)
        return result;
    if (short_side == 0U)
        return SYS_OK;
    if (radius > (short_side - 1U) / 2U)
        radius = (uint16_t)((short_side - 1U) / 2U);

    for (py = y < 0 ? 0 : y; py < bottom && py < (int32_t)OLED_HEIGHT; py++)
    {
        sy = py - y;
        for (px = x < 0 ? 0 : x; px < right && px < (int32_t)OLED_WIDTH; px++)
        {
            sx = px - x;
            if (mode == OLED_UI_OUTLINE)
            {
                //边框是圆角区域中至少有一个外侧四邻点的像素。
                if (!OledUi_InsideRoundRect(sx, sy, width, height, radius) ||
                    (OledUi_InsideRoundRect(sx - 1, sy, width, height, radius) &&
                     OledUi_InsideRoundRect(sx + 1, sy, width, height, radius) &&
                     OledUi_InsideRoundRect(sx, sy - 1, width, height, radius) &&
                     OledUi_InsideRoundRect(sx, sy + 1, width, height, radius)))
                    continue;
            }
            else if (mode == OLED_UI_INVERT)
            {
                uint32_t offset;

                //仅在圆角两侧判断覆盖，主体区域直接反色
                if ((sx < radius || sx >= (int32_t)width - radius) &&
                    !OledUi_InsideRoundRect(sx, sy, width, height, radius))
                    continue;
                //仅反色需要读取SSD1306页布局，写入仍使用Device Ops。
                offset = (uint32_t)py / 8U * OLED_WIDTH + (uint32_t)px;
                on = (oled->framebuffer[offset] & (1U << ((uint32_t)py % 8U))) == 0U;
            }
            else if (mode == OLED_UI_BITMAP)
            {
                size_t offset = (size_t)sy * stride + (uint32_t)sx / 8U;
                on = (data[offset] & (0x80U >> ((uint32_t)sx % 8U))) != 0U;
            }
            result = oled->ops->draw_pixel(oled, (uint16_t)px, (uint16_t)py, on);
            if (result != SYS_OK)
                return result;
        }
    }
    return SYS_OK;
}

//绘制矩形外边缘
SysResult_t OledUi_DrawRect(OledDevice_t *oled,
    int16_t x, int16_t y, uint16_t width, uint16_t height, bool on)
{
    return OledUi_DrawArea(oled, x, y, width, height, 0U, OLED_UI_OUTLINE, on, NULL, 0U);
}

//用指定像素值覆盖矩形内部
SysResult_t OledUi_FillRect(OledDevice_t *oled,
    int16_t x, int16_t y, uint16_t width, uint16_t height, bool on)
{
    return OledUi_DrawArea(oled, x, y, width, height, 0U, OLED_UI_FILL, on, NULL, 0U);
}

SysResult_t OledUi_DrawRoundRect(OledDevice_t *oled,
    int16_t x, int16_t y, uint16_t width, uint16_t height, uint16_t radius, bool on)
{
    return OledUi_DrawArea(oled, x, y, width, height, radius, OLED_UI_OUTLINE, on, NULL, 0U);
}

//按原像素值翻转裁剪区域
SysResult_t OledUi_InvertArea(OledDevice_t *oled,
    int16_t x, int16_t y, uint16_t width, uint16_t height)
{
    return OledUi_DrawArea(oled, x, y, width, height, 0U, OLED_UI_INVERT, false, NULL, 0U);
}

//沿用圆角裁剪和设备写入权限，反色以保留选中文字
SysResult_t OledUi_InvertRoundRect(OledDevice_t *oled,
    int16_t x, int16_t y, uint16_t width, uint16_t height, uint16_t radius)
{
    return OledUi_DrawArea(oled, x, y, width, height, radius, OLED_UI_INVERT, false, NULL, 0U);
}

SysResult_t OledUi_DrawBitmap(OledDevice_t *oled,
    int16_t x, int16_t y, uint16_t width, uint16_t height,
    const uint8_t *data, size_t length)
{
    return OledUi_DrawArea(oled, x, y, width, height, 0U, OLED_UI_BITMAP, false, data, length);
}

//缩小图标时直接采样原位图，不分配中间缓冲
SysResult_t OledUi_DrawIcon16(OledDevice_t *oled, int16_t x, int16_t y,
    uint8_t size, const uint8_t bits[32])
{
    uint32_t row, col, sx, sy;
    SysResult_t result;

    if (bits == NULL || size == 0U || size > 16U ||
        x < 0 || y < 0 || x + size > (int32_t)OLED_WIDTH || y + size > (int32_t)OLED_HEIGHT)
        return SYS_ERR_INVALID_ARG;
    if (size == 16U)
        return OledUi_DrawBitmap(oled, x, y, 16U, 16U, bits, 32U);

    result = OledUi_CheckWritable(oled);
    if (result != SYS_OK)
        return result;
    for (row = 0U; row < size; row++)
    {
        sy = row * 16U / size;
        for (col = 0U; col < size; col++)
        {
            sx = col * 16U / size;
            if ((bits[sy * 2U + sx / 8U] & (0x80U >> (sx % 8U))) != 0U)
            {
                result = oled->ops->draw_pixel(oled, (uint16_t)(x + col), (uint16_t)(y + row), true);
                if (result != SYS_OK)
                    return result;
            }
        }
    }
    return SYS_OK;
}
