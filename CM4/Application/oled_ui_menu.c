#include "oled_ui_menu.h"
#include "oled_ui_draw.h"
#include <stddef.h>

#define OLED_UI_ROW_HEIGHT 8
#define OLED_UI_TITLE_LIMIT 20U
#define OLED_UI_LABEL_LIMIT 18U

//标题与三条菜单行均按8像素对齐。选中行最后反色。
SysResult_t OledUi_MenuRender(OledDevice_t *oled, const OledUiPageState_t *state)
{
    const OledUiPage_t *page;
    const OledUiCursor_t *cursor;
    uint32_t row, index;
    int16_t y;
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
    result = OledUi_DrawString(oled, 3, 0, page->title, OLED_UI_TITLE_LIMIT);
    if (result != SYS_OK)
        return result;

    if (page->type == OLED_UI_PAGE_TYPE_INFO)
        return OledUi_DrawString(oled, 3, OLED_UI_ROW_HEIGHT, page->info, OLED_UI_TITLE_LIMIT);

    cursor = &state->cursor[state->current];
    for (row = 0U, index = cursor->first_visible;
         row < OLED_UI_MENU_VISIBLE && index < page->count; row++, index++)
    {
        y = (int16_t)((row + 1U) * OLED_UI_ROW_HEIGHT);
        result = OledUi_DrawString(oled, 15, y, page->items[index].label, OLED_UI_LABEL_LIMIT);
        if (result != SYS_OK)
            return result;
        if (index == cursor->selected)
        {
            result = OledUi_DrawChar(oled, 3, y, (uint8_t)'>');
            if (result != SYS_OK)
                return result;
            result = OledUi_InvertArea(oled, 0, y, OLED_WIDTH, OLED_UI_ROW_HEIGHT);
            if (result != SYS_OK)
                return result;
        }
    }
    return SYS_OK;
}
