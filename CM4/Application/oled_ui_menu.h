#ifndef CM4_OLED_UI_MENU_H
#define CM4_OLED_UI_MENU_H

#include "oled_device.h"
#include "oled_ui_page.h"

//只在主循环IDLE渲染菜单或静态页。调用方负责备份、回滚和刷新。
SysResult_t OledUi_MenuRender(OledDevice_t *oled, const OledUiPageState_t *state);

#include "oled_ui_anim.h"

SysResult_t OledUi_MenuRenderVisual(OledDevice_t *oled, const OledUiPageState_t *state,
    const OledUiVisual_t *visual);
SysResult_t OledUi_MenuPopup(OledDevice_t *oled, const OledUiPopup_t *popup, uint32_t now);

#endif
