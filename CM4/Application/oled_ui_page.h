//固定页面模型只接收应用主循环的逻辑事件。
#ifndef CM4_OLED_UI_PAGE_H
#define CM4_OLED_UI_PAGE_H

#include <stdint.h>
#include "sys_result.h"

#define OLED_UI_MENU_CAPACITY 5U
#define OLED_UI_MENU_VISIBLE  3U

typedef enum
{
    OLED_UI_PAGE_MONITOR = 0, OLED_UI_PAGE_SYSTEM, OLED_UI_PAGE_CAMERA,
    OLED_UI_PAGE_NETWORK, OLED_UI_PAGE_STORAGE, OLED_UI_PAGE_CAMERA_STATUS,
    OLED_UI_PAGE_CAMERA_FORMAT, OLED_UI_PAGE_STORAGE_INFO, OLED_UI_PAGE_ABOUT,
    OLED_UI_PAGE_COUNT
} OledUiPageId_t;

typedef enum
{
    OLED_UI_EVENT_OPEN_MENU = 1, OLED_UI_EVENT_NEXT, OLED_UI_EVENT_PREVIOUS,
    OLED_UI_EVENT_SELECT, OLED_UI_EVENT_BACK
} OledUiEvent_t;

typedef enum
{
    OLED_UI_PAGE_TYPE_MONITOR, OLED_UI_PAGE_TYPE_MENU, OLED_UI_PAGE_TYPE_INFO
} OledUiPageType_t;

typedef enum
{
    OLED_UI_ITEM_OPEN, OLED_UI_ITEM_BACK
} OledUiItemAction_t;

typedef struct
{
    const char *label;
    OledUiItemAction_t action;
    OledUiPageId_t target;
} OledUiMenuItem_t;

typedef struct
{
    OledUiPageId_t id;
    OledUiPageId_t parent;
    OledUiPageType_t type;
    const char *title;
    const char *info;
    const OledUiMenuItem_t *items;
    uint8_t count;
} OledUiPage_t;

typedef struct
{
    uint8_t selected;
    uint8_t first_visible;
} OledUiCursor_t;

typedef struct
{
    uint32_t revision;
    uint8_t current;
    OledUiCursor_t cursor[OLED_UI_PAGE_COUNT];
} OledUiPageState_t;

//失败不改变状态或输出。根页返回和空页移动成功但不更新版本。
SysResult_t OledUi_PageInit(OledUiPageState_t *state);
SysResult_t OledUi_PageOpen(OledUiPageState_t *state, OledUiPageId_t id);
SysResult_t OledUi_PageBack(OledUiPageState_t *state);
SysResult_t OledUi_PageMoveNext(OledUiPageState_t *state);
SysResult_t OledUi_PageMovePrevious(OledUiPageState_t *state);
SysResult_t OledUi_PageDispatch(OledUiPageState_t *state, uint32_t event);
SysResult_t OledUi_PageGet(OledUiPageId_t id, const OledUiPage_t **page);
SysResult_t OledUi_PageGetCurrent(const OledUiPageState_t *state, const OledUiPage_t **page);
SysResult_t OledUi_PageGetSelected(const OledUiPageState_t *state, const OledUiMenuItem_t **item);

#endif
