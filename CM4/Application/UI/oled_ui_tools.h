#ifndef CM4_OLED_UI_TOOLS_H
#define CM4_OLED_UI_TOOLS_H

#include "oled_device.h"
#include "oled_ui_page.h"
#include <stddef.h>

typedef struct
{
    int32_t values[9]; //各可编辑选项已确认值
    int32_t draft; //当前编辑草稿
    uint32_t opened; //弹窗打开时间，单位ms
    uint32_t entered; //进入视图时间，单位ms
    uint32_t moved; //游标移动时间，单位ms
    uint32_t page_at; //进入工具页时间，单位ms
    int16_t from; //滚动起始偏移
    uint8_t selected; //选中工具项
    uint8_t view; //当前工具子视图
    uint8_t modal; //活动弹窗类型
    uint8_t aux_cursor; //局部列表游标
} OledUiTools_t;

typedef enum
{
    OLED_UI_TOOLS_PARENT = 0,
    OLED_UI_TOOLS_HANDLED,
    OLED_UI_TOOLS_CONTEXT_CHANGED
} OledUiToolsResult_t;

//零初始化。窗口或子视图改变时，调用者清除已排队的旧上下文事件。
OledUiToolsResult_t OledUi_ToolsEvent(OledUiTools_t *ui, uint32_t event, uint32_t now);
//绘制当前工具视图、弹窗和完成帧率
SysResult_t OledUi_ToolsRender(OledDevice_t *oled, const OledUiTools_t *ui,
    uint32_t now, uint32_t fps_tenths);
//计算线性、缓动或弹簧运动的千分比进度
uint16_t OledUi_ToolsProgress(uint8_t curve, uint32_t elapsed, uint32_t duration);

#endif
