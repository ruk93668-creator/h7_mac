//只读页面树与各页游标。页面切换不依赖显示设备。
#include "oled_ui_page.h"
#include <stddef.h>
#include <string.h>

//在编译期计算静态页面或菜单表的元素数
#define OLED_UI_COUNT(array) (sizeof(array) / sizeof((array)[0]))
//在编译期检查各静态表的容量约束
#define OLED_UI_ASSERT(name, condition) typedef char name[(condition) ? 1 : -1]

//系统主菜单入口及各项跳转目标
static const OledUiMenuItem_t s_system[] =
{
    {"Camera", OLED_UI_ITEM_OPEN, OLED_UI_PAGE_CAMERA},
    {"Network", OLED_UI_ITEM_OPEN, OLED_UI_PAGE_NETWORK},
    {"Storage", OLED_UI_ITEM_OPEN, OLED_UI_PAGE_STORAGE},
    {"Diagnostics", OLED_UI_ITEM_OPEN, OLED_UI_PAGE_ABOUT},
    {"Back", OLED_UI_ITEM_BACK, OLED_UI_PAGE_MONITOR},
    {"System Monitor", OLED_UI_ITEM_OPEN, OLED_UI_PAGE_SYSTEM_MONITOR},
    {"UI Tools", OLED_UI_ITEM_OPEN, OLED_UI_PAGE_TOOLS}
};
//Camera子菜单及只读诊断入口
static const OledUiMenuItem_t s_camera[] =
{
    {"Status", OLED_UI_ITEM_OPEN, OLED_UI_PAGE_CAMERA_STATUS},
    {"Format", OLED_UI_ITEM_OPEN, OLED_UI_PAGE_CAMERA_FORMAT},
    {"USB", OLED_UI_ITEM_OPEN, OLED_UI_PAGE_USB},
    {"Back", OLED_UI_ITEM_BACK, OLED_UI_PAGE_SYSTEM}
};
//网络页仅保留返回项，等待正式数据源
static const OledUiMenuItem_t s_network[] = {{"Back", OLED_UI_ITEM_BACK, OLED_UI_PAGE_SYSTEM}};
//Storage子菜单及可用信息入口
static const OledUiMenuItem_t s_storage[] =
{
    {"Info", OLED_UI_ITEM_OPEN, OLED_UI_PAGE_STORAGE_INFO},
    {"Back", OLED_UI_ITEM_BACK, OLED_UI_PAGE_SYSTEM}
};

//系统诊断菜单及各只读详情页入口
static const OledUiMenuItem_t s_system_monitor[] =
{
    {"Tasks", OLED_UI_ITEM_OPEN, OLED_UI_PAGE_TASKS},
    {"Memory", OLED_UI_ITEM_OPEN, OLED_UI_PAGE_MEMORY},
    {"CPU", OLED_UI_ITEM_OPEN, OLED_UI_PAGE_CPU},
    {"Runtime", OLED_UI_ITEM_OPEN, OLED_UI_PAGE_RUNTIME},
    {"Back", OLED_UI_ITEM_BACK, OLED_UI_PAGE_SYSTEM}
};

//固定的页面ID、父子关系和渲染数据表
static const OledUiPage_t s_pages[] =
{
    {OLED_UI_PAGE_MONITOR, OLED_UI_PAGE_COUNT, OLED_UI_PAGE_TYPE_MONITOR, "IPC MONITOR", NULL, NULL, 0},
    {OLED_UI_PAGE_SYSTEM, OLED_UI_PAGE_MONITOR, OLED_UI_PAGE_TYPE_MENU, "SYSTEM MENU", NULL, s_system, OLED_UI_COUNT(s_system)},
    {OLED_UI_PAGE_CAMERA, OLED_UI_PAGE_SYSTEM, OLED_UI_PAGE_TYPE_MENU, "CAMERA", NULL, s_camera, OLED_UI_COUNT(s_camera)},
    {OLED_UI_PAGE_NETWORK, OLED_UI_PAGE_SYSTEM, OLED_UI_PAGE_TYPE_MENU, "NETWORK", NULL, s_network, OLED_UI_COUNT(s_network)},
    {OLED_UI_PAGE_STORAGE, OLED_UI_PAGE_SYSTEM, OLED_UI_PAGE_TYPE_MENU, "STORAGE", NULL, s_storage, OLED_UI_COUNT(s_storage)},
    {OLED_UI_PAGE_CAMERA_STATUS, OLED_UI_PAGE_CAMERA, OLED_UI_PAGE_TYPE_INFO, "CAMERA STATUS", "STATIC PREVIEW", NULL, 0},
    {OLED_UI_PAGE_CAMERA_FORMAT, OLED_UI_PAGE_CAMERA, OLED_UI_PAGE_TYPE_INFO, "CAMERA FORMAT", "STATIC PREVIEW", NULL, 0},
    {OLED_UI_PAGE_STORAGE_INFO, OLED_UI_PAGE_STORAGE, OLED_UI_PAGE_TYPE_INFO, "STORAGE INFO", "STATIC PREVIEW", NULL, 0},
    {OLED_UI_PAGE_ABOUT, OLED_UI_PAGE_SYSTEM, OLED_UI_PAGE_TYPE_INFO, "DIAGNOSTICS", "OLED DEVICE MANAGEMENT - READ ONLY PREVIEW", NULL, 0},
    {OLED_UI_PAGE_USB, OLED_UI_PAGE_CAMERA, OLED_UI_PAGE_TYPE_INFO, "USB VIDEO", "NO DATA", NULL, 0},
    {OLED_UI_PAGE_SYSTEM_MONITOR, OLED_UI_PAGE_SYSTEM, OLED_UI_PAGE_TYPE_MENU, "SYSTEM MONITOR", NULL, s_system_monitor, OLED_UI_COUNT(s_system_monitor)},
    {OLED_UI_PAGE_TASKS, OLED_UI_PAGE_SYSTEM_MONITOR, OLED_UI_PAGE_TYPE_INFO, "TASKS", "NOT PUBLISHED", NULL, 0},
    {OLED_UI_PAGE_MEMORY, OLED_UI_PAGE_SYSTEM_MONITOR, OLED_UI_PAGE_TYPE_INFO, "MEMORY", "NOT PUBLISHED", NULL, 0},
    {OLED_UI_PAGE_CPU, OLED_UI_PAGE_SYSTEM_MONITOR, OLED_UI_PAGE_TYPE_INFO, "CPU", "NOT PUBLISHED", NULL, 0},
    {OLED_UI_PAGE_RUNTIME, OLED_UI_PAGE_SYSTEM_MONITOR, OLED_UI_PAGE_TYPE_INFO, "RUNTIME", "NOT PUBLISHED", NULL, 0},
    {OLED_UI_PAGE_TOOLS, OLED_UI_PAGE_SYSTEM, OLED_UI_PAGE_TYPE_INFO, "UI TOOLS", "SELECT A TOOL", NULL, 0}
};

OLED_UI_ASSERT(OledUiPageCount, OLED_UI_COUNT(s_pages) == OLED_UI_PAGE_COUNT);
OLED_UI_ASSERT(OledUiStateSize, sizeof(OledUiPageState_t) <= 40U);
OLED_UI_ASSERT(OledUiItemCount, OLED_UI_COUNT(s_system) <= OLED_UI_MENU_CAPACITY &&
    OLED_UI_COUNT(s_camera) <= OLED_UI_MENU_CAPACITY &&
    OLED_UI_COUNT(s_network) <= OLED_UI_MENU_CAPACITY &&
    OLED_UI_COUNT(s_storage) <= OLED_UI_MENU_CAPACITY &&
    OLED_UI_COUNT(s_system_monitor) <= OLED_UI_MENU_CAPACITY);

//每次读取都检查固定容量内的状态，失败时不返回部分结果。
static SysResult_t OledUi_PageValidate(const OledUiPageState_t *state)
{
    uint32_t i, count, selected, first, last_first;

    if (state == NULL)
        return SYS_ERR_INVALID_ARG;
    if (state->current >= OLED_UI_PAGE_COUNT)
        return SYS_ERR_INVALID_STATE;

    for (i = 0U; i < OLED_UI_PAGE_COUNT; i++)
    {
        count = s_pages[i].count;
        selected = state->cursor[i].selected;
        first = state->cursor[i].first_visible;
        if (count == 0U)
        {
            if (selected != 0U || first != 0U)
                return SYS_ERR_INVALID_STATE;
            continue;
        }

        last_first = count > OLED_UI_MENU_VISIBLE ? count - OLED_UI_MENU_VISIBLE : 0U;
        if (selected >= count || first > last_first ||
            selected < first || selected >= first + OLED_UI_MENU_VISIBLE)
            return SYS_ERR_INVALID_STATE;
    }
    return SYS_OK;
}

//从固定表取得指定页面描述
SysResult_t OledUi_PageGet(OledUiPageId_t id, const OledUiPage_t **page)
{
    if (page == NULL || (uint32_t)id >= OLED_UI_PAGE_COUNT)
        return SYS_ERR_INVALID_ARG;

    *page = &s_pages[id];
    return SYS_OK;
}

//验证状态并取得当前页面描述
SysResult_t OledUi_PageGetCurrent(const OledUiPageState_t *state, const OledUiPage_t **page)
{
    SysResult_t result;

    if (page == NULL)
        return SYS_ERR_INVALID_ARG;
    result = OledUi_PageValidate(state);
    if (result == SYS_OK)
        *page = &s_pages[state->current];
    return result;
}

//取得当前菜单项，空页面返回错误
SysResult_t OledUi_PageGetSelected(const OledUiPageState_t *state, const OledUiMenuItem_t **item)
{
    const OledUiPage_t *page;
    SysResult_t result;

    if (item == NULL)
        return SYS_ERR_INVALID_ARG;
    result = OledUi_PageGetCurrent(state, &page);
    if (result != SYS_OK)
        return result;
    if (page->count == 0U)
        return SYS_ERR_NOT_FOUND;

    *item = &page->items[state->cursor[state->current].selected];
    return SYS_OK;
}

//初始化页面状态并打开四行监控首页
SysResult_t OledUi_PageInit(OledUiPageState_t *state)
{
    if (state == NULL)
        return SYS_ERR_INVALID_ARG;
    memset(state, 0, sizeof(*state));
    return SYS_OK;
}

//打开指定页面并保留各页原有游标
SysResult_t OledUi_PageOpen(OledUiPageState_t *state, OledUiPageId_t id)
{
    const OledUiPage_t *target;
    SysResult_t result = OledUi_PageGet(id, &target);

    if (result != SYS_OK)
        return result;
    result = OledUi_PageValidate(state);
    if (result != SYS_OK || id == state->current)
        return result;
    if (target->parent != state->current)
        return SYS_ERR_INVALID_STATE;

    state->current = (uint8_t)id;
    state->revision++;
    return SYS_OK;
}

//返回父页，根页面留在原处
SysResult_t OledUi_PageBack(OledUiPageState_t *state)
{
    const OledUiPage_t *page;
    SysResult_t result = OledUi_PageGetCurrent(state, &page);

    if (result != SYS_OK || page->id == OLED_UI_PAGE_MONITOR)
        return result;

    state->current = (uint8_t)page->parent;
    state->revision++;
    return SYS_OK;
}

static SysResult_t OledUi_PageMove(OledUiPageState_t *state, int32_t step)
{
    const OledUiPage_t *page;
    OledUiCursor_t *cursor;
    int32_t selected;
    SysResult_t result = OledUi_PageGetCurrent(state, &page);

    if (result != SYS_OK)
        return result;
    cursor = &state->cursor[state->current];
    selected = (int32_t)cursor->selected + step;
    if (selected < 0 || selected >= page->count)
        return SYS_OK;

    cursor->selected = (uint8_t)selected;
    if (selected < cursor->first_visible)
        cursor->first_visible = (uint8_t)selected;
    else if (selected >= (int32_t)cursor->first_visible + (int32_t)OLED_UI_MENU_VISIBLE)
        cursor->first_visible = (uint8_t)(selected + 1 - (int32_t)OLED_UI_MENU_VISIBLE);
    state->revision++;
    return SYS_OK;
}

//将当前菜单游标向下一项移动
SysResult_t OledUi_PageMoveNext(OledUiPageState_t *state)
{
    return OledUi_PageMove(state, 1);
}

//将当前菜单游标向上一项移动
SysResult_t OledUi_PageMovePrevious(OledUiPageState_t *state)
{
    return OledUi_PageMove(state, -1);
}

//按当前页上下文处理导航事件
SysResult_t OledUi_PageDispatch(OledUiPageState_t *state, uint32_t event)
{
    const OledUiMenuItem_t *item;
    const OledUiPage_t *page;
    SysResult_t result;

    switch (event)
    {
        case OLED_UI_EVENT_OPEN_MENU:
            result = OledUi_PageGetCurrent(state, &page);
            if (result == SYS_OK && page->id == OLED_UI_PAGE_MONITOR)
                return OledUi_PageOpen(state, OLED_UI_PAGE_SYSTEM);
            return result;
        case OLED_UI_EVENT_NEXT: return OledUi_PageMoveNext(state);
        case OLED_UI_EVENT_PREVIOUS: return OledUi_PageMovePrevious(state);
        case OLED_UI_EVENT_BACK: return OledUi_PageBack(state);
        case OLED_UI_EVENT_SELECT:
            result = OledUi_PageGetSelected(state, &item);
            if (result == SYS_ERR_NOT_FOUND)
                return SYS_OK;
            if (result != SYS_OK)
                return result;
            if (item->action == OLED_UI_ITEM_BACK)
                return OledUi_PageBack(state);
            return OledUi_PageOpen(state, item->target);
        default: return SYS_ERR_INVALID_ARG;
    }
}
