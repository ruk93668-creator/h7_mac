#include "oled_ui_menu.h"
#include "oled_ui_draw.h"
#include <stddef.h>

#define OLED_UI_ROW_HEIGHT 8
#define OLED_UI_TITLE_LIMIT 20U
#define OLED_UI_LABEL_LIMIT 18U

//标题与三条菜单行均按8像素对齐。选中行最后反色。
SysResult_t OledUi_MenuRenderVisual(OledDevice_t *oled, const OledUiPageState_t *state,
    const OledUiVisual_t *visual)
{
    const OledUiPage_t *page;
    const OledUiCursor_t *cursor;
    uint32_t row, index;
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
        label = visual != NULL ? OledUi_AnimText(page->info, OLED_UI_TITLE_LIMIT, visual->age) : page->info;
        result = OledUi_DrawString(oled, 3, OLED_UI_ROW_HEIGHT, label, OLED_UI_TITLE_LIMIT);
        if (result != SYS_OK)
            return result;
    }
    else
    {
        cursor = &state->cursor[state->current];
        for (row = 0U, index = cursor->first_visible;
             row < OLED_UI_MENU_VISIBLE && index < page->count; row++, index++)
        {
            y = (int16_t)((row + 1U) * OLED_UI_ROW_HEIGHT);
            if (visual != NULL)
                y += visual->scroll_y;
            label = page->items[index].label;
            if (visual != NULL && index == cursor->selected)
                label = OledUi_AnimText(label, OLED_UI_LABEL_LIMIT, visual->age);
            result = OledUi_DrawString(oled, 15, y, label, OLED_UI_LABEL_LIMIT);
            if (result != SYS_OK)
                return result;
            if (index == cursor->selected)
            {
                result = OledUi_DrawChar(oled, 3, y, (uint8_t)'>');
                if (result != SYS_OK)
                    return result;
                highlight_y = visual != NULL ? visual->highlight_y : y;
            }
        }

        result = OledUi_InvertArea(oled, 0, highlight_y, OLED_WIDTH, OLED_UI_ROW_HEIGHT);
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

    result = OledUi_DrawString(oled, 3, 0, page->title, OLED_UI_TITLE_LIMIT);
    if (result != SYS_OK || visual == NULL || visual->transition_width == 0U)
        return result;
    return OledUi_DrawLine(oled, 0, 7, visual->transition_width - 1, 7, true);
}

SysResult_t OledUi_MenuRender(OledDevice_t *oled, const OledUiPageState_t *state)
{
    return OledUi_MenuRenderVisual(oled, state, NULL);
}

SysResult_t OledUi_MenuPopup(OledDevice_t *oled, const OledUiPopup_t *popup, uint32_t now)
{
    uint32_t age;
    uint16_t height;
    int16_t y;
    SysResult_t result;

    if (popup == NULL)
        return SYS_ERR_INVALID_ARG;
    if (!popup->active || now - popup->opened >= popup->duration)
        return SYS_OK;

    age = now - popup->opened;
    height = age >= 120U ? 28U : (uint16_t)(8U + 20U * age / 120U);
    y = (int16_t)((OLED_HEIGHT - height) / 2U);
    result = OledUi_FillRect(oled, 8, y, 112, height, false);
    if (result == SYS_OK)
        result = OledUi_DrawRect(oled, 8, y, 112, height, true);
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
