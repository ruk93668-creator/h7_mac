#include "oled_ui_compat.h"
#include "oled_ui_draw.h"
#include "oled_ui_assets.inc"

typedef struct
{
    const char *title;
    const char *brief;
    int16_t min, max, step;
    uint8_t icon;
} OledUiCompatItem_t;

enum
{
    TOOL_LAYOUT, TOOL_CURSOR, TOOL_MOTION, TOOL_FONT, TOOL_WINDOW, TOOL_DURATION,
    TOOL_SPEED, TOOL_LIGHT, TOOL_FPS, TOOL_TEXT, TOOL_GALLERY, TOOL_EMPTY,
    TOOL_SMALL, TOOL_EMPTY_WINDOW, TOOL_SHAPES, TOOL_ABOUT, TOOL_BACK, TOOL_COUNT
};
enum { TOOL_VIEW_MENU, TOOL_VIEW_GALLERY, TOOL_VIEW_EMPTY, TOOL_VIEW_SMALL,
       TOOL_VIEW_SHAPES, TOOL_VIEW_ABOUT };
enum { TOOL_MODAL_NONE, TOOL_MODAL_EDIT, TOOL_MODAL_TEXT, TOOL_MODAL_EMPTY };

static const OledUiCompatItem_t s_tools[] =
{
    {"Menu layout", "Layout", 0, 1, 1, 0}, {"Cursor style", "Cursor", 0, 5, 1, 0},
    {"Animation curve", "Curve", 0, 2, 1, 1}, {"Font size", "Font", 0, 3, 1, 1},
    {"Window shape", "Window", 0, 1, 1, 1}, {"Duration ms", "Time", 0, 600, 20, 3},
    {"Scroll speed", "Speed", 0, 300, 25, 3}, {"Light mode", "Light", 0, 1, 1, 4},
    {"FPS overlay", "FPS", 0, 1, 1, 0}, {"Text window", "Text", 0, 0, 0, 1},
    {"Bitmap / GIF", "Gallery", 0, 0, 0, 2}, {"Empty page", "Empty", 0, 0, 0, 4},
    {"Small area list", "Area", 0, 0, 0, 0}, {"Empty window", "Window", 0, 0, 0, 1},
    {"Shape gallery", "Shapes", 0, 0, 0, 2}, {"About device", "About", 0, 0, 0, 3},
    {"Back", "Back", 0, 0, 0, 2}
};

#define TOOL_COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))
#define TOOL_TRY(expr) do { SysResult_t r_ = (expr); if (r_ != SYS_OK) return r_; } while (0)

static uint8_t OledUi_CompatWidth(uint8_t height)
{
    return height == 8U ? 6U : height == 12U ? 7U : height == 16U ? 8U : 10U;
}

static const uint8_t *OledUi_CompatGlyph(uint16_t code, uint8_t height, uint8_t *width)
{
    uint32_t i;
    if (code > 0xffU)
    {
        //字库只覆盖参考项目收录的GB2312字符，未收录时安全显示问号。
#define FIND_HAN(a) for (i = 0; i < TOOL_COUNT_OF(a); i++) \
    if ((a)[i].code == code) { *width = height; return (a)[i].data; }
        if (height == 8U) { FIND_HAN(s_chinese_8) }
        else if (height == 12U) { FIND_HAN(s_chinese_12) }
        else if (height == 16U) { FIND_HAN(s_chinese_16) }
        else { FIND_HAN(s_chinese_20) }
#undef FIND_HAN
    }
    i = code >= 32U && code <= 126U ? code - 32U : (uint32_t)'?' - 32U;
    *width = OledUi_CompatWidth(height);
    if (height == 8U) return s_ascii_8[i];
    if (height == 12U) return s_ascii_12[i];
    if (height == 16U) return s_ascii_16[i];
    return s_ascii_20[i];
}

static SysResult_t OledUi_CompatTextClip(OledDevice_t *oled, int16_t x, int16_t y,
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
        clip_left > clip_right || (height != 8U && height != 12U && height != 16U && height != 20U))
        return SYS_ERR_INVALID_ARG;
    TOOL_TRY(OledUi_CheckWritable(oled));

    //字模按列和8像素页排列，只写亮点并同时裁剪窗口与屏幕。
    for (i = 0U; i < length && left < clip_right && text[i] != '\0'; i++)
    {
        code = (uint8_t)text[i];
        if (code >= 0xa1U && code <= 0xf7U && i + 1U < length &&
            (uint8_t)text[i + 1U] >= 0xa1U && (uint8_t)text[i + 1U] <= 0xfeU)
            code = (uint16_t)((code << 8) | (uint8_t)text[++i]);

        glyph = OledUi_CompatGlyph(code, height, &width);
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
                    TOOL_TRY(oled->ops->draw_pixel(oled, (uint16_t)px, (uint16_t)py, true));
            }
        }
        left += width;
    }
    return SYS_OK;
}

SysResult_t OledUi_CompatText(OledDevice_t *oled, int16_t x, int16_t y,
    int16_t clip_left, int16_t clip_right, const char *text, size_t length, uint8_t height)
{
    return OledUi_CompatTextClip(oled, x, y, clip_left, clip_right, 0, OLED_HEIGHT,
        text, length, height);
}

uint16_t OledUi_CompatProgress(uint8_t curve, uint32_t elapsed, uint32_t duration)
{
    uint32_t t, u, i, segment;
    //PID风格用有界阻尼响应表，避免采样速度改变手感或状态发散。
    static const uint16_t spring[] = {0,490,900,1080,1090,1030,997,1000};
    if (duration == 0U || elapsed >= duration)
        return 1000U;
    //把非常大的时长与时间差同比缩放，避免乘法溢出和64位除法。
    while (duration > UINT32_MAX / 1000U)
    {
        elapsed >>= 1;
        duration >>= 1;
    }
    t = elapsed * 1000U / duration;
    if (t > 999U) t = 999U;
    if (curve == 1U)
    {
        u = 1000U - t;
        return (uint16_t)(1000U - u * u / 1000U);
    }
    if (curve == 2U)
    {
        segment = t * (TOOL_COUNT_OF(spring) - 1U);
        i = segment / 1000U;
        return (uint16_t)((int32_t)spring[i] + ((int32_t)spring[i + 1U] - spring[i]) *
            (int32_t)(segment % 1000U) / 1000);
    }
    return (uint16_t)t;
}

static uint32_t OledUi_CompatDuration(const OledUiCompat_t *ui)
{
    return 120U + (uint32_t)ui->values[TOOL_DURATION];
}

static int16_t OledUi_CompatPosition(const OledUiCompat_t *ui, uint32_t now)
{
    int32_t target = ui->selected * 16;
    uint16_t progress = OledUi_CompatProgress((uint8_t)ui->values[TOOL_MOTION],
        now - ui->moved, OledUi_CompatDuration(ui));
    return (int16_t)(ui->from + (target - ui->from) * progress / 1000);
}

static bool OledUi_CompatApplyEvent(OledUiCompat_t *ui, uint32_t event, uint32_t now)
{
    int32_t value;
    const OledUiCompatItem_t *item = &s_tools[ui->selected];
    if (ui->modal >= TOOL_MODAL_TEXT && now - ui->opened >= 3500U)
        ui->modal = TOOL_MODAL_NONE;
    if (ui->modal != TOOL_MODAL_NONE)
    {
        //编辑经过草稿。确认才写本地设置，取消不穿透到页面。
        if (event == OLED_UI_EVENT_BACK || event == OLED_UI_EVENT_SELECT)
        {
            if (event == OLED_UI_EVENT_SELECT && ui->modal == TOOL_MODAL_EDIT)
                ui->values[ui->selected] = ui->draft;
            ui->modal = TOOL_MODAL_NONE;
            ui->entered = now;
        }
        else if (ui->modal == TOOL_MODAL_EDIT &&
            (event == OLED_UI_EVENT_NEXT || event == OLED_UI_EVENT_PREVIOUS))
        {
            value = ui->draft + (event == OLED_UI_EVENT_NEXT ? item->step : -item->step);
            ui->draft = value < item->min ? item->min : (value > item->max ? item->max : value);
        }
        return true;
    }
    if (ui->view != TOOL_VIEW_MENU)
    {
        if (event == OLED_UI_EVENT_BACK)
        {
            ui->view = TOOL_VIEW_MENU;
            ui->entered = now;
            ui->page_at = now;
        }
        else if (ui->view == TOOL_VIEW_SMALL &&
            (event == OLED_UI_EVENT_NEXT || event == OLED_UI_EVENT_PREVIOUS))
        {
            value = (int32_t)ui->aux_cursor + (event == OLED_UI_EVENT_NEXT ? 1 : -1);
            if (value >= 0 && value < 4)
                ui->aux_cursor = (uint8_t)value;
        }
        return true;
    }
    if (event == OLED_UI_EVENT_BACK || (event == OLED_UI_EVENT_SELECT && ui->selected == TOOL_BACK))
        return false;
    if (event == OLED_UI_EVENT_NEXT || event == OLED_UI_EVENT_PREVIOUS)
    {
        value = (int32_t)ui->selected + (event == OLED_UI_EVENT_NEXT ? 1 : -1);
        if (value >= 0 && value < TOOL_COUNT)
        {
            ui->from = OledUi_CompatPosition(ui, now);
            ui->moved = now;
            ui->selected = (uint8_t)value;
            ui->entered = now;
        }
    }
    else if (event == OLED_UI_EVENT_SELECT)
    {
        ui->opened = now;
        if (ui->selected == TOOL_LIGHT || ui->selected == TOOL_FPS)
            ui->values[ui->selected] = !ui->values[ui->selected];
        else if (ui->selected < TOOL_LIGHT)
        {
            ui->draft = ui->values[ui->selected];
            ui->modal = TOOL_MODAL_EDIT;
        }
        else if (ui->selected == TOOL_TEXT)
            ui->modal = TOOL_MODAL_TEXT;
        else if (ui->selected == TOOL_EMPTY_WINDOW)
            ui->modal = TOOL_MODAL_EMPTY;
        else if (ui->selected == TOOL_GALLERY || ui->selected == TOOL_EMPTY)
        {
            ui->view = ui->selected == TOOL_GALLERY ? TOOL_VIEW_GALLERY : TOOL_VIEW_EMPTY;
            ui->page_at = now;
        }
        else if (ui->selected >= TOOL_SMALL && ui->selected < TOOL_BACK)
        {
            ui->view = ui->selected == TOOL_SMALL ? TOOL_VIEW_SMALL :
                (ui->selected == TOOL_SHAPES ? TOOL_VIEW_SHAPES : TOOL_VIEW_ABOUT);
            ui->page_at = now;
            ui->aux_cursor = 0U;
        }
    }
    return true;
}

OledUiCompatResult_t OledUi_CompatEvent(OledUiCompat_t *ui, uint32_t event, uint32_t now)
{
    uint8_t modal, view;
    uint32_t opened;

    if (ui == NULL || ui->selected >= TOOL_COUNT)
        return OLED_UI_COMPAT_HANDLED;
    modal = ui->modal;
    view = ui->view;
    opened = ui->opened;
    if (!OledUi_CompatApplyEvent(ui, event, now))
        return OLED_UI_COMPAT_PARENT;

    //文字窗口超时后同拍重新打开时，类型相同但输入上下文已经更新。
    if (view != ui->view || modal != ui->modal ||
        (ui->modal != TOOL_MODAL_NONE && opened != ui->opened))
        return OLED_UI_COMPAT_CONTEXT_CHANGED;
    return OLED_UI_COMPAT_HANDLED;
}

static SysResult_t OledUi_CompatCursor(OledDevice_t *oled, int16_t x, int16_t y,
    uint16_t w, uint16_t h, uint8_t style)
{
    int16_t row;
    uint16_t inset;

    switch (style)
    {
        case 0U: return OledUi_InvertArea(oled, x, y, w, h);
        case 1U:
            //圆角反色用对称短行，中心区域只调用一次反色。
            for (row = 0; row < 3; row++)
            {
                inset = row == 0 ? 3U : (row == 1 ? 1U : 0U);
                TOOL_TRY(OledUi_InvertArea(oled, x + inset, y + row, w - 2U * inset, 1));
                TOOL_TRY(OledUi_InvertArea(oled, x + inset, y + h - row - 1U, w - 2U * inset, 1));
            }
            return OledUi_InvertArea(oled, x, y + 3, w, h - 6U);
        case 2U: return OledUi_DrawRect(oled, x, y, w, h, true);
        case 3U: return OledUi_DrawRoundRect(oled, x, y, w, h, 4U, true);
        case 4U: return OledUi_InvertArea(oled, x, y + h - 2U, w, 2U);
        default: return SYS_OK;
    }
}

static void OledUi_CompatNumber(char *out, uint32_t value, bool decimal)
{
    char rev[10];
    uint32_t i = 0U, j = 0U;
    if (decimal)
    {
        rev[i++] = (char)('0' + value % 10U);
        rev[i++] = (char)('0' + value / 10U % 10U);
        rev[i++] = '.';
        value /= 100U;
    }
    do
    {
        rev[i++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U && i < sizeof(rev));
    while (i != 0U)
        out[j++] = rev[--i];
    out[j] = '\0';
}

static const char *OledUi_CompatValue(uint8_t item, int32_t value, char *number)
{
    static const char * const cursors[] = {"Solid", "Round solid", "Outline", "Round outline", "Bar", "Hidden"};
    static const char * const curves[] = {"Linear", "Ease out", "PID spring"};
    static const char * const fonts[] = {"8 px", "12 px", "16 px", "20 px"};
    if (item == TOOL_LAYOUT) return value ? "Tiles" : "List";
    if (item == TOOL_CURSOR) return cursors[value];
    if (item == TOOL_MOTION) return curves[value];
    if (item == TOOL_FONT) return fonts[value];
    if (item == TOOL_WINDOW) return value ? "Round" : "Rectangle";
    OledUi_CompatNumber(number, (uint32_t)(item == TOOL_DURATION ? value + 120 : value + 100),
        item == TOOL_SPEED);
    return number;
}

static SysResult_t OledUi_CompatModal(OledDevice_t *oled, const OledUiCompat_t *ui, uint32_t now)
{
    char number[12];
    uint32_t age = now - ui->opened;
    uint16_t height = (uint16_t)(8U + 22U * OledUi_CompatProgress(
        (uint8_t)ui->values[TOOL_MOTION], age, 180U) / 1090U);
    int16_t top;

    if (ui->modal == TOOL_MODAL_NONE || (ui->modal >= TOOL_MODAL_TEXT && age >= 3500U))
        return SYS_OK;
    if (age >= 180U) height = 30U;
    top = (int16_t)(32U - height) / 2;
    TOOL_TRY(OledUi_FillRect(oled, 5, top, 118, height, false));
    if (ui->values[TOOL_WINDOW])
        TOOL_TRY(OledUi_DrawRoundRect(oled, 5, top, 118, height, 4, true));
    else
        TOOL_TRY(OledUi_DrawRect(oled, 5, top, 118, height, true));
    if (age < 180U || ui->modal == TOOL_MODAL_EMPTY) return SYS_OK;
    TOOL_TRY(OledUi_CompatText(oled, 10, 3, 9, 120, s_tools[ui->selected].title, 32, 8));
    if (ui->modal == TOOL_MODAL_TEXT)
        TOOL_TRY(OledUi_CompatText(oled, 10, 11, 9, 120, "Read only preview", 17, 8));
    else
    {
        const char *value = OledUi_CompatValue(ui->selected, ui->draft, number);
        TOOL_TRY(OledUi_CompatText(oled, 10, 11, 9, 120, value, 18, 8));
        TOOL_TRY(OledUi_DrawRect(oled, 9, 19, 109, 3, true));
        TOOL_TRY(OledUi_FillRect(oled, 10, 20,
            (uint16_t)(107 * ui->draft / s_tools[ui->selected].max), 1, true));
    }
    return OledUi_CompatText(oled, 10, 23, 9, 120, "OK     BACK CANCEL", 18, 8);
}

static SysResult_t OledUi_CompatList(OledDevice_t *oled, const OledUiCompat_t *ui, uint32_t now)
{
    static const uint8_t heights[] = {8,12,16,20};
    uint8_t height = heights[ui->values[TOOL_FONT]];
    uint8_t rows = 24U / height;
    uint8_t first = ui->selected < rows ? 0U : (uint8_t)(ui->selected + 1U - rows);
    uint8_t i, n;
    int16_t y, cursor = 8 + (OledUi_CompatPosition(ui, now) - first * 16) * height / 16;
    uint32_t offset, step = 45U - (uint32_t)ui->values[TOOL_SPEED] / 8U;
    const char *label;

    if (cursor < 8) cursor = 8;
    if (cursor > 32 - height) cursor = 32 - height;
    TOOL_TRY(OledUi_CompatText(oled, 2, 0, 0, 126, "UI TOOLS", 8, 8));
    for (i = 0U; i < rows; i++)
    {
        n = first + i;
        y = (int16_t)(8U + height * i);
        label = s_tools[n].title;
        for (offset = 0U; label[offset] != '\0'; offset++)
            ;
        offset *= OledUi_CompatWidth(height);
        if (offset > 110U && n == ui->selected)
        {
            uint32_t excess = offset - 110U;
            uint32_t age = now - ui->entered;
            offset = age > 500U ? ((age - 500U) / step) % (excess + 20U) : 0U;
            if (offset > excess) offset = excess;
        }
        else
            offset = 0U;
        TOOL_TRY(OledUi_CompatText(oled, (int16_t)(10 - (int32_t)offset), y, 10, 120, label, 32, height));
        if (n == TOOL_LIGHT || n == TOOL_FPS)
        {
            TOOL_TRY(OledUi_DrawRect(oled, 2, y + 2, 6, 5, true));
            if (ui->values[n])
                TOOL_TRY(OledUi_FillRect(oled, 3, y + 3, 4, 3, true));
        }
        else if (n == ui->selected)
            TOOL_TRY(OledUi_CompatText(oled, 1, y, 0, 8, ">", 1, 8));
    }
    TOOL_TRY(OledUi_DrawLine(oled, 126, 8, 126, 31, true));
    TOOL_TRY(OledUi_FillRect(oled, 125, (int16_t)(8U + ui->selected * 20U / (TOOL_COUNT - 1U)), 3, 4, true));
    return OledUi_CompatCursor(oled, 0, cursor, 124, height, (uint8_t)ui->values[TOOL_CURSOR]);
}

static SysResult_t OledUi_CompatTiles(OledDevice_t *oled, const OledUiCompat_t *ui, uint32_t now)
{
    uint8_t i, first = (uint8_t)(ui->selected / 3U * 3U);
    int16_t x, cursor = 2 + (OledUi_CompatPosition(ui, now) - first * 16) * 42 / 16;
    if (cursor < 2) cursor = 2;
    if (cursor > 86) cursor = 86;
    TOOL_TRY(OledUi_CompatText(oled, 3, 0, 0, 126, s_tools[ui->selected].title, 32, 8));
    for (i = 0U; i < 3U && i + first < TOOL_COUNT; i++)
    {
        const OledUiCompatItem_t *item = &s_tools[first + i];
        x = (int16_t)(2U + i * 42U);
        TOOL_TRY(OledUi_DrawBitmap(oled, x + 13, 8, 16, 16, s_icons[item->icon], 32U));
        TOOL_TRY(OledUi_CompatText(oled, x + 2, 24, x, x + 40, item->brief, 7, 8));
    }
    return OledUi_CompatCursor(oled, cursor, 8, 40, 24, (uint8_t)ui->values[TOOL_CURSOR]);
}

static SysResult_t OledUi_CompatSmall(OledDevice_t *oled, uint8_t selected)
{
    static const char * const s_labels[] = {"Area one", "Scrollable label", "Area three", "Area four"};
    uint32_t first = selected > 1U ? selected - 1U : 0U;
    uint32_t i;
    TOOL_TRY(OledUi_CompatText(oled, 1, 0, 0, 128, "CUSTOM VIEWPORT", 15, 8));
    TOOL_TRY(OledUi_DrawRect(oled, 16, 9, 96, 21, true));
    for (i = 0U; i < 2U; i++)
        TOOL_TRY(OledUi_CompatTextClip(oled, 19, (int16_t)(11 + 9U*i), 18, 110,
            10, 29, s_labels[first+i], 20, 8));
    return OledUi_InvertArea(oled, 18, (int16_t)(11 + 9U*(selected-first)), 92, 8);
}

static bool OledUi_CompatEllipsePoint(int32_t x, int32_t y, int32_t rx, int32_t ry)
{
    return x*x*ry*ry + y*y*rx*rx <= rx*rx*ry*ry;
}

//形状样例尺寸在调用点固定，描边取椭圆内侧四邻接轮廓。
static SysResult_t OledUi_CompatEllipse(OledDevice_t *oled, int16_t cx, int16_t cy,
    int16_t rx, int16_t ry, bool upper_arc)
{
    int32_t x,y;
    for (y = -ry; y <= (upper_arc ? 0 : ry); y++)
        for (x = -rx; x <= rx; x++)
            if (OledUi_CompatEllipsePoint(x,y,rx,ry) &&
                !(OledUi_CompatEllipsePoint(x+1,y,rx,ry) && OledUi_CompatEllipsePoint(x-1,y,rx,ry) &&
                  OledUi_CompatEllipsePoint(x,y+1,rx,ry) && OledUi_CompatEllipsePoint(x,y-1,rx,ry)))
                TOOL_TRY(oled->ops->draw_pixel(oled, (uint16_t)(cx+x), (uint16_t)(cy+y), true));
    return SYS_OK;
}

static SysResult_t OledUi_CompatShapes(OledDevice_t *oled)
{
    TOOL_TRY(OledUi_CompatText(oled, 1, 0, 0, 128, "SHAPES", 6, 8));
    TOOL_TRY(OledUi_DrawLine(oled, 3, 29, 14, 11, true));
    TOOL_TRY(OledUi_DrawLine(oled, 14, 11, 25, 29, true));
    TOOL_TRY(OledUi_DrawLine(oled, 3, 29, 25, 29, true));
    TOOL_TRY(OledUi_CompatEllipse(oled, 44, 21, 9, 9, false));
    TOOL_TRY(OledUi_CompatEllipse(oled, 76, 21, 13, 7, false));
    return OledUi_CompatEllipse(oled, 109, 24, 12, 10, true);
}

static SysResult_t OledUi_CompatDissolve(OledDevice_t *oled, uint32_t level)
{
    static const uint8_t s_order[] = {0U, 2U, 3U, 1U};
    uint16_t x, y;
    for (y = 0U; y < OLED_HEIGHT; y++)
        for (x = 0U; x < OLED_WIDTH; x++)
            if (s_order[(y & 1U) * 2U + (x & 1U)] >= level &&
                (oled->framebuffer[y / 8U * OLED_WIDTH + x] & (1U << (y % 8U))) != 0U)
                TOOL_TRY(oled->ops->draw_pixel(oled, x, y, false));
    return SYS_OK;
}

SysResult_t OledUi_CompatRender(OledDevice_t *oled, const OledUiCompat_t *ui,
    uint32_t now, uint32_t fps_tenths)
{
    char number[12];
    uint32_t age;
    if (ui == NULL || ui->selected >= TOOL_COUNT)
        return SYS_ERR_INVALID_ARG;
    TOOL_TRY(OledUi_CheckWritable(oled));
    if (oled->ops->clear == NULL)
        return SYS_ERR_NOT_SUPPORTED;
    TOOL_TRY(oled->ops->clear(oled));
    if (ui->view == TOOL_VIEW_EMPTY)
        return SYS_OK;
    if (ui->view == TOOL_VIEW_GALLERY)
    {
        age = now - ui->opened;
        TOOL_TRY(OledUi_DrawBitmap(oled, 5, 8, 16, 16, s_cube[age / 80U % TOOL_COUNT_OF(s_cube)], 32U));
        TOOL_TRY(OledUi_CompatText(oled, 29, 3, 24, 128, "Bitmap + GIF", 12, 12));
        TOOL_TRY(OledUi_CompatText(oled, 29, 21, 24, 128, "中文 128x32", 16, 8));
    }
    else if (ui->view == TOOL_VIEW_SMALL)
        TOOL_TRY(OledUi_CompatSmall(oled, ui->aux_cursor));
    else if (ui->view == TOOL_VIEW_SHAPES)
        TOOL_TRY(OledUi_CompatShapes(oled));
    else if (ui->view == TOOL_VIEW_ABOUT)
    {
        TOOL_TRY(OledUi_CompatText(oled, 1, 0, 0, 128, "STM32H747 CM4", 13, 8));
        TOOL_TRY(OledUi_CompatText(oled, 1, 8, 0, 128, "SSD1306 128x32", 14, 8));
        TOOL_TRY(OledUi_CompatText(oled, 1, 16, 0, 128, "OLED_UI / APACHE", 16, 8));
        TOOL_TRY(OledUi_CompatText(oled, 1, 24, 0, 128, "REF 8f02069", 11, 8));
    }
    else if (ui->values[TOOL_LAYOUT])
        TOOL_TRY(OledUi_CompatTiles(oled, ui, now));
    else
        TOOL_TRY(OledUi_CompatList(oled, ui, now));

    if (ui->values[TOOL_FPS])
    {
        TOOL_TRY(OledUi_FillRect(oled, 96, 0, 32, 8, false));
        if (fps_tenths == 0U)
            TOOL_TRY(OledUi_CompatText(oled, 98, 0, 96, 128, "N/A", 3, 8));
        else
        {
            OledUi_CompatNumber(number, fps_tenths * 10U, true);
            TOOL_TRY(OledUi_CompatText(oled, 98, 0, 96, 128, number, 5, 8));
        }
    }
    TOOL_TRY(OledUi_CompatModal(oled, ui, now));
    age = now - ui->page_at;
    if (age < 160U)
        TOOL_TRY(OledUi_CompatDissolve(oled, age / 40U));
    if (ui->values[TOOL_LIGHT])
        TOOL_TRY(OledUi_InvertArea(oled, 0, 0, OLED_WIDTH, OLED_HEIGHT));
    return SYS_OK;
}
