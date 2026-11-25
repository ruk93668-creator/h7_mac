#include "oled_ui_tools.h"
#include "oled_ui_draw.h"
#include "oled_ui_font.h"
#include "Assets/oled_ui_bitmaps.inc"

typedef struct
{
    const char *title;
    const char *brief;
    int16_t min, max, step;
    uint8_t icon;
} OledUiToolsItem_t;

enum
{
    TOOL_LAYOUT, TOOL_CURSOR, TOOL_MOTION, TOOL_FONT, TOOL_WINDOW, TOOL_DURATION,
    TOOL_SPEED, TOOL_LIGHT, TOOL_FPS, TOOL_TEXT, TOOL_GALLERY, TOOL_EMPTY,
    TOOL_SMALL, TOOL_EMPTY_WINDOW, TOOL_SHAPES, TOOL_ABOUT, TOOL_BACK, TOOL_COUNT
};
enum { TOOL_VIEW_MENU, TOOL_VIEW_GALLERY, TOOL_VIEW_EMPTY, TOOL_VIEW_SMALL,
       TOOL_VIEW_SHAPES, TOOL_VIEW_ABOUT };
enum { TOOL_MODAL_NONE, TOOL_MODAL_EDIT, TOOL_MODAL_TEXT, TOOL_MODAL_EMPTY };

//工具项名称、编辑范围、步进和图标索引
static const OledUiToolsItem_t s_tools[] =
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

//编译期计算固定资源表的元素数
#define TOOL_COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))
//遇到绘制错误时保留错误码并停止绘制本帧
#define TOOL_TRY(expr) do { SysResult_t r_ = (expr); if (r_ != SYS_OK) return r_; } while (0)

//读取工具页运动时长，单位ms
static uint32_t OledUi_ToolsDuration(const OledUiTools_t *ui)
{
    return 120U + (uint32_t)ui->values[TOOL_DURATION];
}

//求列表在当前时刻的垂直滚动位置
static int16_t OledUi_ToolsPosition(const OledUiTools_t *ui, uint32_t now)
{
    int32_t target = ui->selected * 16;
    uint16_t progress = OledUi_AnimProgress((uint8_t)ui->values[TOOL_MOTION],
        now - ui->moved, OledUi_ToolsDuration(ui));
    return (int16_t)(ui->from + (target - ui->from) * progress / 1000);
}

//在当前工具视图或弹窗上下文处理输入
static bool OledUi_ToolsApplyEvent(OledUiTools_t *ui, uint32_t event, uint32_t now)
{
    int32_t value;
    const OledUiToolsItem_t *item = &s_tools[ui->selected];
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
            ui->from = OledUi_ToolsPosition(ui, now);
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

OledUiToolsResult_t OledUi_ToolsEvent(OledUiTools_t *ui, uint32_t event, uint32_t now)
{
    uint8_t modal, view;
    uint32_t opened;

    if (ui == NULL || ui->selected >= TOOL_COUNT)
        return OLED_UI_TOOLS_HANDLED;
    modal = ui->modal;
    view = ui->view;
    opened = ui->opened;
    if (!OledUi_ToolsApplyEvent(ui, event, now))
        return OLED_UI_TOOLS_PARENT;

    //文字窗口超时后同拍重新打开时，类型相同但输入上下文已经更新。
    if (view != ui->view || modal != ui->modal ||
        (ui->modal != TOOL_MODAL_NONE && opened != ui->opened))
        return OLED_UI_TOOLS_CONTEXT_CHANGED;
    return OLED_UI_TOOLS_HANDLED;
}

//按所选样式绘制高亮或指针
SysResult_t OledUi_DrawCursor(OledDevice_t *oled, int16_t x, int16_t y,
    uint16_t w, uint16_t h, uint8_t style)
{
    switch (style)
    {
        case 0U: return OledUi_InvertArea(oled, x, y, w, h);
        case 1U: return OledUi_InvertRoundRect(oled, x, y, w, h, 3U);
        case 2U: return OledUi_DrawRect(oled, x, y, w, h, true);
        case 3U: return OledUi_DrawRoundRect(oled, x, y, w, h, 4U, true);
        case 4U: return OledUi_InvertArea(oled, x, y + h - 2U, w, 2U);
        default: return SYS_OK;
    }
}

//从现有16像素位图等间隔取样，为大字体标签让出高度
SysResult_t OledUi_DrawMenuIcon(OledDevice_t *oled, int16_t x, int16_t y,
    uint8_t size, uint8_t icon)
{
    uint32_t row, col, sx, sy;
    const uint8_t *bits;

    if (size == 0U || size > 16U || icon >= TOOL_COUNT_OF(s_icons) ||
        x < 0 || y < 0 || x + size > OLED_WIDTH || y + size > OLED_HEIGHT)
        return SYS_ERR_INVALID_ARG;
    bits = s_icons[icon];
    if (size == 16U)
        return OledUi_DrawBitmap(oled, x, y, 16U, 16U, bits, 32U);
    TOOL_TRY(OledUi_CheckWritable(oled));
    for (row = 0U; row < size; row++)
        for (col = 0U; col < size; col++)
        {
            sx = col * 16U / size;
            sy = row * 16U / size;
            if ((bits[sy * 2U + sx / 8U] & (0x80U >> (sx % 8U))) != 0U)
                TOOL_TRY(oled->ops->draw_pixel(oled, (uint16_t)(x + col), (uint16_t)(y + row), true));
        }
    return SYS_OK;
}

//按同一规则读取工具页和普通菜单的配置
void OledUi_ToolsReadStyle(const OledUiTools_t *ui, OledUiMenuStyle_t *style)
{
    static const uint8_t heights[] = {8U, 12U, 16U, 20U};

    style->font_height = heights[ui->values[TOOL_FONT]];
    style->duration_ms = (uint16_t)OledUi_ToolsDuration(ui);
    style->scroll_ms = (uint16_t)(32000U / (100U + (uint32_t)ui->values[TOOL_SPEED]));
    style->cursor_style = (uint8_t)ui->values[TOOL_CURSOR];
    style->curve = (uint8_t)ui->values[TOOL_MOTION];
    style->tiles = ui->values[TOOL_LAYOUT] != 0;
    style->round_window = ui->values[TOOL_WINDOW] != 0;
}

//格式化非负整数或定点小数
static void OledUi_ToolsNumber(char *out, uint32_t value, bool decimal)
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

//将编辑值转换为显示名称或数字
static const char *OledUi_ToolsValue(uint8_t item, int32_t value, char *number)
{
    static const char * const cursors[] = {"Solid", "Round solid", "Outline", "Round outline", "Bar", "Hidden"};
    static const char * const curves[] = {"Linear", "Ease out", "PID spring"};
    static const char * const fonts[] = {"8 px", "12 px", "16 px", "20 px"};
    if (item == TOOL_LAYOUT) return value ? "Tiles" : "List";
    if (item == TOOL_CURSOR) return cursors[value];
    if (item == TOOL_MOTION) return curves[value];
    if (item == TOOL_FONT) return fonts[value];
    if (item == TOOL_WINDOW) return value ? "Round" : "Rectangle";
    OledUi_ToolsNumber(number, (uint32_t)(item == TOOL_DURATION ? value + 120 : value + 100),
        item == TOOL_SPEED);
    return number;
}

//绘制活动弹窗及本地编辑草稿
static SysResult_t OledUi_ToolsModal(OledDevice_t *oled, const OledUiTools_t *ui, uint32_t now)
{
    char number[12];
    uint32_t age = now - ui->opened;
    uint32_t duration = 3U * OledUi_ToolsDuration(ui) / 5U;
    uint16_t progress = OledUi_AnimProgress((uint8_t)ui->values[TOOL_MOTION], age, duration);
    uint16_t height = (uint16_t)(8U + 22U * (progress < 1000U ? progress : 1000U) / 1000U);
    int16_t top;

    if (ui->modal == TOOL_MODAL_NONE || (ui->modal >= TOOL_MODAL_TEXT && age >= 3500U))
        return SYS_OK;
    top = (int16_t)(32U - height) / 2;
    TOOL_TRY(OledUi_FillRect(oled, 5, top, 118, height, false));
    if (ui->values[TOOL_WINDOW])
        TOOL_TRY(OledUi_DrawRoundRect(oled, 5, top, 118, height, 4, true));
    else
        TOOL_TRY(OledUi_DrawRect(oled, 5, top, 118, height, true));
    if (age < duration || ui->modal == TOOL_MODAL_EMPTY) return SYS_OK;
    TOOL_TRY(OledUi_FontText(oled, 10, 3, 9, 120, s_tools[ui->selected].title, 32, 8));
    if (ui->modal == TOOL_MODAL_TEXT)
        TOOL_TRY(OledUi_FontText(oled, 10, 11, 9, 120, "Read only preview", 17, 8));
    else
    {
        const char *value = OledUi_ToolsValue(ui->selected, ui->draft, number);
        TOOL_TRY(OledUi_FontText(oled, 10, 11, 9, 120, value, 18, 8));
        TOOL_TRY(OledUi_DrawRect(oled, 9, 19, 109, 3, true));
        TOOL_TRY(OledUi_FillRect(oled, 10, 20,
            (uint16_t)(107 * ui->draft / s_tools[ui->selected].max), 1, true));
    }
    return OledUi_FontText(oled, 10, 23, 9, 120, "OK     BACK CANCEL", 18, 8);
}

//在当前滚动位置绘制工具列表
static SysResult_t OledUi_ToolsList(OledDevice_t *oled, const OledUiTools_t *ui, uint32_t now)
{
    static const uint8_t heights[] = {8,12,16,20};
    uint8_t height = heights[ui->values[TOOL_FONT]];
    uint8_t rows = 24U / height;
    uint8_t first = ui->selected < rows ? 0U : (uint8_t)(ui->selected + 1U - rows);
    uint8_t i, n;
    int16_t y, cursor = 8 + (OledUi_ToolsPosition(ui, now) - first * 16) * height / 16;
    uint32_t offset;
    OledUiMenuStyle_t style;
    const char *label;

    OledUi_ToolsReadStyle(ui, &style);
    if (cursor < 8) cursor = 8;
    if (cursor > 32 - height) cursor = 32 - height;
    TOOL_TRY(OledUi_FontText(oled, 2, 0, 0, 126, "UI TOOLS", 8, 8));
    for (i = 0U; i < rows; i++)
    {
        n = first + i;
        y = (int16_t)(8U + height * i);
        label = s_tools[n].title;
        for (offset = 0U; label[offset] != '\0'; offset++)
            ;
        offset *= OledUi_FontWidth(height);
        if (offset > 110U && n == ui->selected)
        {
            uint32_t excess = offset - 110U;
            uint32_t age = now - ui->entered;
            offset = age > 500U ? ((age - 500U) / style.scroll_ms) % (excess + 20U) : 0U;
            if (offset > excess) offset = excess;
        }
        else
            offset = 0U;
        TOOL_TRY(OledUi_FontText(oled, (int16_t)(10 - (int32_t)offset), y, 10, 120, label, 32, height));
        if (n == TOOL_LIGHT || n == TOOL_FPS)
        {
            TOOL_TRY(OledUi_DrawRect(oled, 2, y + 2, 6, 5, true));
            if (ui->values[n])
                TOOL_TRY(OledUi_FillRect(oled, 3, y + 3, 4, 3, true));
        }
        else if (n == ui->selected && style.cursor_style != 5U)
            TOOL_TRY(OledUi_FontText(oled, 1, y, 0, 8, ">", 1, 8));
    }
    TOOL_TRY(OledUi_DrawLine(oled, 126, 8, 126, 31, true));
    TOOL_TRY(OledUi_FillRect(oled, 125, (int16_t)(8U + ui->selected * 20U / (TOOL_COUNT - 1U)), 3, 4, true));
    return OledUi_DrawCursor(oled, 0, cursor, 124, height, (uint8_t)ui->values[TOOL_CURSOR]);
}

//绘制当前三项图标及选中状态
static SysResult_t OledUi_ToolsTiles(OledDevice_t *oled, const OledUiTools_t *ui, uint32_t now)
{
    uint8_t i, first = (uint8_t)(ui->selected / 3U * 3U);
    OledUiMenuStyle_t style;
    uint8_t size, columns;
    const char *label;
    int16_t x, cursor = 2 + (OledUi_ToolsPosition(ui, now) - first * 16) * 42 / 16;
    OledUi_ToolsReadStyle(ui, &style);
    size = 24U - style.font_height;
    columns = 36U / OledUi_FontWidth(style.font_height);
    if (cursor < 2) cursor = 2;
    if (cursor > 86) cursor = 86;
    TOOL_TRY(OledUi_FontText(oled, 3, 0, 0, 126, s_tools[ui->selected].title, 32, 8));
    for (i = 0U; i < 3U && i + first < TOOL_COUNT; i++)
    {
        const OledUiToolsItem_t *item = &s_tools[first + i];
        x = (int16_t)(2U + i * 42U);
        TOOL_TRY(OledUi_DrawMenuIcon(oled, (int16_t)(x + (40U - size) / 2U), 8, size, item->icon));
        label = item->brief;
        if (i + first == ui->selected)
            label = OledUi_AnimText(label, columns, now - ui->entered, style.scroll_ms);
        TOOL_TRY(OledUi_FontText(oled, x + 2, (int16_t)(8U + size), x + 2, x + 38,
            label, columns, style.font_height));
    }
    return OledUi_DrawCursor(oled, cursor, 8, 40, 24, (uint8_t)ui->values[TOOL_CURSOR]);
}

//绘制受局部裁剪框限制的列表
static SysResult_t OledUi_ToolsSmall(OledDevice_t *oled, uint8_t selected)
{
    static const char * const s_labels[] = {"Area one", "Scrollable label", "Area three", "Area four"};
    uint32_t first = selected > 1U ? selected - 1U : 0U;
    uint32_t i;
    TOOL_TRY(OledUi_FontText(oled, 1, 0, 0, 128, "CUSTOM VIEWPORT", 15, 8));
    TOOL_TRY(OledUi_DrawRect(oled, 16, 9, 96, 21, true));
    for (i = 0U; i < 2U; i++)
        TOOL_TRY(OledUi_FontTextClip(oled, 19, (int16_t)(11 + 9U*i), 18, 110,
            10, 29, s_labels[first+i], 20, 8));
    return OledUi_InvertArea(oled, 18, (int16_t)(11 + 9U*(selected-first)), 92, 8);
}

//判断像素是否落在椭圆判定范围内
static bool OledUi_ToolsEllipsePoint(int32_t x, int32_t y, int32_t rx, int32_t ry)
{
    return x*x*ry*ry + y*y*rx*rx <= rx*rx*ry*ry;
}

//形状样例尺寸在调用点固定，描边取椭圆内侧四邻接轮廓。
static SysResult_t OledUi_ToolsEllipse(OledDevice_t *oled, int16_t cx, int16_t cy,
    int16_t rx, int16_t ry, bool upper_arc)
{
    int32_t x,y;
    for (y = -ry; y <= (upper_arc ? 0 : ry); y++)
        for (x = -rx; x <= rx; x++)
            if (OledUi_ToolsEllipsePoint(x,y,rx,ry) &&
                !(OledUi_ToolsEllipsePoint(x+1,y,rx,ry) && OledUi_ToolsEllipsePoint(x-1,y,rx,ry) &&
                  OledUi_ToolsEllipsePoint(x,y+1,rx,ry) && OledUi_ToolsEllipsePoint(x,y-1,rx,ry)))
                TOOL_TRY(oled->ops->draw_pixel(oled, (uint16_t)(cx+x), (uint16_t)(cy+y), true));
    return SYS_OK;
}

//绘制工具页现有图形组件组合
static SysResult_t OledUi_ToolsShapes(OledDevice_t *oled)
{
    TOOL_TRY(OledUi_FontText(oled, 1, 0, 0, 128, "SHAPES", 6, 8));
    TOOL_TRY(OledUi_DrawLine(oled, 3, 29, 14, 11, true));
    TOOL_TRY(OledUi_DrawLine(oled, 14, 11, 25, 29, true));
    TOOL_TRY(OledUi_DrawLine(oled, 3, 29, 25, 29, true));
    TOOL_TRY(OledUi_ToolsEllipse(oled, 44, 21, 9, 9, false));
    TOOL_TRY(OledUi_ToolsEllipse(oled, 76, 21, 13, 7, false));
    return OledUi_ToolsEllipse(oled, 109, 24, 12, 10, true);
}

//用固定像素顺序推进页面过渡
static SysResult_t OledUi_ToolsDissolve(OledDevice_t *oled, uint32_t level)
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

//绘制当前工具视图、弹窗和完成帧率
SysResult_t OledUi_ToolsRender(OledDevice_t *oled, const OledUiTools_t *ui,
    uint32_t now, uint32_t fps_tenths)
{
    OledUiMenuStyle_t style;
    uint32_t age, transition_ms;
    if (ui == NULL || ui->selected >= TOOL_COUNT)
        return SYS_ERR_INVALID_ARG;
    TOOL_TRY(OledUi_CheckWritable(oled));
    if (oled->ops->clear == NULL)
        return SYS_ERR_NOT_SUPPORTED;
    TOOL_TRY(oled->ops->clear(oled));
    OledUi_ToolsReadStyle(ui, &style);
    if (ui->view == TOOL_VIEW_EMPTY)
        return OledUi_ToolsApplyDisplay(oled, ui, fps_tenths, 0);
    if (ui->view == TOOL_VIEW_GALLERY)
    {
        age = now - ui->opened;
        TOOL_TRY(OledUi_DrawBitmap(oled, 5, 8, 16, 16, s_cube[age / 80U % TOOL_COUNT_OF(s_cube)], 32U));
        TOOL_TRY(OledUi_FontText(oled, 29, 3, 24, 128, "Bitmap + GIF", 12, 12));
        TOOL_TRY(OledUi_FontText(oled, 29, 21, 24, 128, "中文 128x32", 16, 8));
    }
    else if (ui->view == TOOL_VIEW_SMALL)
        TOOL_TRY(OledUi_ToolsSmall(oled, ui->aux_cursor));
    else if (ui->view == TOOL_VIEW_SHAPES)
        TOOL_TRY(OledUi_ToolsShapes(oled));
    else if (ui->view == TOOL_VIEW_ABOUT)
    {
        TOOL_TRY(OledUi_FontText(oled, 1, 0, 0, 128, "STM32H747 CM4", 13, 8));
        TOOL_TRY(OledUi_FontText(oled, 1, 8, 0, 128, "SSD1306 128x32", 14, 8));
        TOOL_TRY(OledUi_FontText(oled, 1, 16, 0, 128, "OLED_UI / APACHE", 16, 8));
        TOOL_TRY(OledUi_FontText(oled, 1, 24, 0, 128, "REF 8f02069", 11, 8));
    }
    else if (ui->values[TOOL_LAYOUT])
        TOOL_TRY(OledUi_ToolsTiles(oled, ui, now));
    else
        TOOL_TRY(OledUi_ToolsList(oled, ui, now));

    TOOL_TRY(OledUi_ToolsModal(oled, ui, now));
    age = now - ui->page_at;
    transition_ms = style.duration_ms * 2U / 3U;
    if (age < transition_ms)
        TOOL_TRY(OledUi_ToolsDissolve(oled, OledUi_AnimProgress(style.curve, age, transition_ms) / 250U));
    return OledUi_ToolsApplyDisplay(oled, ui, fps_tenths, 0);
}

//最后叠加完成帧率和明暗样式，由App统一处理提交和回滚
SysResult_t OledUi_ToolsApplyDisplay(OledDevice_t *oled, const OledUiTools_t *ui,
    uint32_t fps_tenths, int16_t fps_y)
{
    char number[12];

    if (ui->values[TOOL_FPS])
    {
        TOOL_TRY(OledUi_FillRect(oled, 96, fps_y, 32, 8, false));
        if (fps_tenths == 0U)
            TOOL_TRY(OledUi_FontText(oled, 98, fps_y, 96, 128, "N/A", 3, 8));
        else
        {
            OledUi_ToolsNumber(number, fps_tenths * 10U, true);
            TOOL_TRY(OledUi_FontText(oled, 98, fps_y, 96, 128, number, 5, 8));
        }
    }
    if (ui->values[TOOL_LIGHT])
        TOOL_TRY(OledUi_InvertArea(oled, 0, 0, OLED_WIDTH, OLED_HEIGHT));
    return SYS_OK;
}
