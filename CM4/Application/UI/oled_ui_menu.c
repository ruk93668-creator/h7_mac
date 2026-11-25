#include "oled_ui_menu.h"
#include "oled_ui_draw.h"
#include "oled_ui_font.h"
#include "oled_ui_tools.h"
#include <stddef.h>

//菜单每个条目的行高，单位像素
#define OLED_UI_ROW_HEIGHT 8
//列表选中块左右各留两像素
#define OLED_UI_HIGHLIGHT_MARGIN 2
//菜单标题可读取的最大字节数
#define OLED_UI_TITLE_LIMIT 20U
//普通菜单标签显示的最大字符数
#define OLED_UI_LABEL_LIMIT 18U

//使用当前全局字号排列图标和标签，确保总高度不超过24像素
static SysResult_t OledUi_MenuTiles(OledDevice_t *oled, const OledUiPage_t *page,
    const OledUiCursor_t *cursor, const OledUiVisual_t *visual, const OledUiMenuStyle_t *style)
{
    uint8_t first = OledUi_MenuFirst(cursor, style);
    uint8_t size = 24U - style->font_height;
    uint8_t columns = 36U / OledUi_FontWidth(style->font_height);
    uint8_t i, index;
    int16_t x;
    const char *label;
    SysResult_t result;

    for (i = 0U, index = first; i < 3U && index < page->count; i++, index++)
    {
        x = (int16_t)(2U + i * 42U);
        result = OledUi_DrawMenuIcon(oled, (int16_t)(x + (40U - size) / 2U),
            8, size, (uint8_t)(page->items[index].target % 5U));
        if (result != SYS_OK)
            return result;
        label = page->items[index].label;
        if (visual != NULL && index == cursor->selected)
            label = OledUi_AnimText(label, columns, visual->age, style->scroll_ms);
        result = OledUi_FontText(oled, x + 2, (int16_t)(8U + size), x + 2, x + 38,
            label, columns, style->font_height);
        if (result != SYS_OK)
            return result;
    }

    x = visual == NULL ? (int16_t)(2U + (cursor->selected - first) * 42U) :
        visual->highlight_pos;
    return OledUi_DrawCursor(oled, x, 8, 40, 24, style->cursor_style);
}

//标题与三条菜单行均按8像素对齐。选中行最后反色。
SysResult_t OledUi_MenuRenderVisual(OledDevice_t *oled, const OledUiPageState_t *state,
    const OledUiVisual_t *visual, const OledUiStatus_t *status, const OledUiMenuStyle_t *style)
{
    const OledUiPage_t *page;
    const OledUiCursor_t *cursor;
    uint32_t row, index;
    uint8_t height = style == NULL ? OLED_UI_ROW_HEIGHT : style->font_height;
    uint8_t cursor_style = style == NULL ? 1U : style->cursor_style;
    uint8_t width = height == OLED_UI_ROW_HEIGHT ? OLED_UI_LABEL_LIMIT :
        (uint8_t)(110U / OledUi_FontWidth(height));
    uint16_t scroll_ms = style == NULL ? 0U : style->scroll_ms;
    int16_t y, highlight_y = 0;
    const char *label;
    SysResult_t result = OledUi_PageGetCurrent(state, &page);

    if (result != SYS_OK)
        return result;
    if (page->type == OLED_UI_PAGE_TYPE_MONITOR)
        return SYS_ERR_NOT_SUPPORTED;

    result = OledUi_CheckWritable(oled);
    if (result != SYS_OK)
        return result;
    if (oled->ops->clear == NULL)
        return SYS_ERR_NOT_SUPPORTED;

    result = oled->ops->clear(oled);
    if (result != SYS_OK)
        return result;
    if (page->type == OLED_UI_PAGE_TYPE_INFO)
    {
        if (status == NULL)
        {
            label = visual != NULL ? OledUi_AnimText(page->info, OLED_UI_TITLE_LIMIT, visual->age, scroll_ms) : page->info;
            result = OledUi_DrawString(oled, 3, OLED_UI_ROW_HEIGHT, label, OLED_UI_TITLE_LIMIT);
            if (result != SYS_OK)
                return result;
        }
    }
    else if (style != NULL && style->tiles)
    {
        result = OledUi_MenuTiles(oled, page, &state->cursor[state->current], visual, style);
        if (result != SYS_OK)
            return result;
    }
    else
    {
        cursor = &state->cursor[state->current];
        for (row = 0U, index = OledUi_MenuFirst(cursor, style);
             row < 24U / height && index < page->count; row++, index++)
        {
            y = (int16_t)(OLED_UI_ROW_HEIGHT + row * height);
            if (visual != NULL)
                y += visual->scroll_y;
            label = page->items[index].label;
            if (visual != NULL && index == cursor->selected)
                label = OledUi_AnimText(label, width, visual->age, scroll_ms);
            if (height == OLED_UI_ROW_HEIGHT)
                result = OledUi_DrawString(oled, 15, y, label, width);
            else
                result = OledUi_FontTextClip(oled, 15, y, 15, 125, 8, OLED_HEIGHT, label, width, height);
            if (result != SYS_OK)
                return result;
            if (index == cursor->selected)
            {
                if (cursor_style != 5U)
                    result = OledUi_DrawChar(oled, 3, (int16_t)(y + (height - 8U) / 2U), (uint8_t)'>');
                if (result != SYS_OK)
                    return result;
                highlight_y = visual != NULL ? visual->highlight_pos : y;
            }
        }

        //各级列表共用当前光标样式，默认实心圆角
        result = OledUi_DrawCursor(oled, OLED_UI_HIGHLIGHT_MARGIN, highlight_y,
            OLED_WIDTH - 2U * OLED_UI_HIGHLIGHT_MARGIN, height, cursor_style);
        if (result != SYS_OK)
            return result;
        if (visual != NULL && visual->scroll_y < 0)
        {
            //上滑时仅清除标题带，防止菜单行穿入标题。
            result = OledUi_FillRect(oled, 0, 0, OLED_WIDTH, OLED_UI_ROW_HEIGHT, false);
            if (result != SYS_OK)
                return result;
        }
    }

    if (status != NULL && status->first_row > 0U && status->first_row < 4U)
        for (row = status->first_row; row < 4U; row++)
        {
            label = status->lines[row - status->first_row];
            if (visual != NULL)
                label = OledUi_AnimText(label, OLED_UI_TITLE_LIMIT, visual->age, scroll_ms);
            result = OledUi_DrawString(oled, 3, (int16_t)(row * OLED_UI_ROW_HEIGHT), label, OLED_UI_TITLE_LIMIT);
            if (result != SYS_OK)
                return result;
        }

    result = OledUi_DrawString(oled, 3, 0, page->title, OLED_UI_TITLE_LIMIT);
    if (result != SYS_OK || visual == NULL || visual->transition_width == 0U)
        return result;
    return OledUi_DrawLine(oled, 0, 7, visual->transition_width - 1, 7, true);
}

SysResult_t OledUi_MenuRender(OledDevice_t *oled, const OledUiPageState_t *state)
{
    return OledUi_MenuRenderVisual(oled, state, NULL, NULL, NULL);
}

//把活动弹窗叠加至当前可写帧
SysResult_t OledUi_MenuPopup(OledDevice_t *oled, const OledUiPopup_t *popup, uint32_t now,
    const OledUiMenuStyle_t *style)
{
    uint32_t age;
    uint16_t height, progress;
    uint16_t duration = style == NULL ? 120U : style->duration_ms;
    uint8_t curve = style == NULL ? 0U : style->curve;
    int16_t y;
    SysResult_t result;

    if (popup == NULL)
        return SYS_ERR_INVALID_ARG;
    if (!popup->active || now - popup->opened >= popup->duration)
        return SYS_OK;

    age = now - popup->opened;
    progress = OledUi_AnimProgress(curve, age, duration);
    height = (uint16_t)(8U + 20U * (progress < 1000U ? progress : 1000U) / 1000U);
    y = (int16_t)((OLED_HEIGHT - height) / 2U);
    result = OledUi_FillRect(oled, 8, y, 112, height, false);
    if (result == SYS_OK)
        result = style != NULL && style->round_window ?
            OledUi_DrawRoundRect(oled, 8, y, 112, height, 3U, true) :
            OledUi_DrawRect(oled, 8, y, 112, height, true);
    if (result != SYS_OK || height != 28U)
        return result;

    result = OledUi_DrawString(oled, 13, 3, popup->title, OLED_UI_POPUP_TEXT - 1U);
    if (result == SYS_OK)
        result = OledUi_DrawString(oled, 13, 11, popup->body, OLED_UI_POPUP_TEXT - 1U);
    if (result == SYS_OK)
        result = OledUi_DrawString(oled, 13, 19,
            popup->confirm ? "SEL OK  BACK" : "SEL/BACK CLOSE", OLED_UI_POPUP_TEXT - 1U);
    return result;
}
