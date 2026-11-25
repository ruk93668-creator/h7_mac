#ifndef CM4_OLED_UI_MENU_H
#define CM4_OLED_UI_MENU_H

#include "oled_device.h"
#include "oled_ui_page.h"

//只在主循环IDLE渲染菜单或静态页。调用方负责备份、回滚和刷新。
SysResult_t OledUi_MenuRender(OledDevice_t *oled, const OledUiPageState_t *state);

#include "oled_ui_anim.h"
#include "oled_ui_status.h"

SysResult_t OledUi_MenuRenderVisual(OledDevice_t *oled, const OledUiPageState_t *state,
    const OledUiVisual_t *visual, const OledUiStatus_t *status, const OledUiMenuStyle_t *style);
//把活动弹窗叠加至当前可写帧
SysResult_t OledUi_MenuPopup(OledDevice_t *oled, const OledUiPopup_t *popup, uint32_t now,
    const OledUiMenuStyle_t *style);

#endif
