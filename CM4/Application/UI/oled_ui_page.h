//固定页面模型只接收应用主循环的逻辑事件。
#ifndef CM4_OLED_UI_PAGE_H
#define CM4_OLED_UI_PAGE_H

#include <stdint.h>
#include "sys_result.h"

//每个页面允许声明的最大菜单项数
#define OLED_UI_MENU_CAPACITY 7U
//屏幕同时显示的最大菜单行数
#define OLED_UI_MENU_VISIBLE  3U

typedef enum
{
    OLED_UI_PAGE_MONITOR = 0, OLED_UI_PAGE_SYSTEM, OLED_UI_PAGE_CAMERA,
    OLED_UI_PAGE_NETWORK, OLED_UI_PAGE_STORAGE, OLED_UI_PAGE_CAMERA_STATUS,
    OLED_UI_PAGE_CAMERA_FORMAT, OLED_UI_PAGE_STORAGE_INFO, OLED_UI_PAGE_ABOUT,
    OLED_UI_PAGE_USB, OLED_UI_PAGE_SYSTEM_MONITOR, OLED_UI_PAGE_TASKS,
    OLED_UI_PAGE_MEMORY, OLED_UI_PAGE_CPU, OLED_UI_PAGE_RUNTIME, OLED_UI_PAGE_TOOLS, OLED_UI_PAGE_COUNT
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
    OledUiItemAction_t action; //选中后执行的导航动作
    OledUiPageId_t target; //导航目标页
} OledUiMenuItem_t;

typedef struct
{
    OledUiPageId_t id; //固定页面编号
    OledUiPageId_t parent; //返回目标
    OledUiPageType_t type; //渲染类型
    const char *title;
    const char *info;
    const OledUiMenuItem_t *items;
    uint8_t count; //菜单项总数
} OledUiPage_t;

typedef struct
{
    uint8_t selected; //选中项索引
    uint8_t first_visible; //首个可见项索引
} OledUiCursor_t;

typedef struct
{
    uint32_t revision; //界面状态更新序号
    uint8_t current; //当前页号
    OledUiCursor_t cursor[OLED_UI_PAGE_COUNT]; //各固定页面的游标
} OledUiPageState_t;

//失败不改变状态或输出。根页返回和空页移动成功但不更新版本。
SysResult_t OledUi_PageInit(OledUiPageState_t *state);
//打开指定页面并保留各页原有游标
SysResult_t OledUi_PageOpen(OledUiPageState_t *state, OledUiPageId_t id);
//返回父页，根页面留在原处
SysResult_t OledUi_PageBack(OledUiPageState_t *state);
//将当前菜单游标向下一项移动
SysResult_t OledUi_PageMoveNext(OledUiPageState_t *state);
//将当前菜单游标向上一项移动
SysResult_t OledUi_PageMovePrevious(OledUiPageState_t *state);
//按当前页上下文处理导航事件
SysResult_t OledUi_PageDispatch(OledUiPageState_t *state, uint32_t event);
//从固定表取得指定页面描述
SysResult_t OledUi_PageGet(OledUiPageId_t id, const OledUiPage_t **page);
//验证状态并取得当前页面描述
SysResult_t OledUi_PageGetCurrent(const OledUiPageState_t *state, const OledUiPage_t **page);
//取得当前菜单项，空页面返回错误
SysResult_t OledUi_PageGetSelected(const OledUiPageState_t *state, const OledUiMenuItem_t **item);

#endif
