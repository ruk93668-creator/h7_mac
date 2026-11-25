#ifndef CM4_OLED_UI_ANIM_H
#define CM4_OLED_UI_ANIM_H

#include "oled_ui_page.h"
#include <stdbool.h>

//弹窗每行文本容量，包含NUL
#define OLED_UI_POPUP_TEXT 17U

//从工具页已提交的设置生成每帧共享显示样式
typedef struct
{
    uint16_t duration_ms; //游标和页面过渡时长
    uint16_t scroll_ms; //走马灯推进一个字符所需的时间
    uint8_t font_height; //列表标签像素高度
    uint8_t cursor_style; //光标样式编号
    uint8_t curve; //运动曲线编号
    bool tiles; //启用图标网格
    bool round_window; //使用圆角窗口边框
} OledUiMenuStyle_t;

//获取线性、缓动或弹簧曲线的千分比进度
uint16_t OledUi_AnimProgress(uint8_t curve, uint32_t elapsed, uint32_t duration);

//计算当前可见窗口的首项，保留原页面导航状态
uint8_t OledUi_MenuFirst(const OledUiCursor_t *cursor, const OledUiMenuStyle_t *style);

typedef struct
{
    uint32_t entered; //进入页面时刻，单位ms
    uint32_t moved; //高亮动作起点，单位ms
    uint8_t page; //上次采样页号
    uint8_t first; //上次可见首项
    uint8_t from_pos; //高亮起始纵坐标
    uint8_t target_pos; //高亮目标纵坐标
    int8_t scroll; //列表滚动方向
    bool entering; //正在过渡
    bool moving; //高亮正在移动
} OledUiAnim_t;

typedef struct
{
    uint8_t highlight_pos; //当前高亮坐标，列表为纵向，图标为横向
    int8_t scroll_y; //当前列表纵向偏移
    uint8_t transition_width; //过渡遮罩宽度
    uint32_t age; //动画已经经过的毫秒数
} OledUiVisual_t;

typedef struct
{
    uint32_t opened; //弹窗打开时间，单位ms
    uint16_t duration; //弹窗保持时长，单位ms
    bool active; //弹窗是否可见
    bool confirm; //是否允许确认关闭
    char title[OLED_UI_POPUP_TEXT]; //标题副本
    char body[OLED_UI_POPUP_TEXT]; //正文副本
} OledUiPopup_t;

//动画状态须零初始化。采样只修改视觉状态，不回写页面逻辑。
void OledUi_AnimSample(OledUiAnim_t *anim, const OledUiPageState_t *state,
    uint32_t now, const OledUiMenuStyle_t *style, OledUiVisual_t *visual);
//返回给定停留时间下长标签的可见起点
const char *OledUi_AnimText(const char *text, uint32_t width, uint32_t age, uint16_t step_ms);

//弹窗只存受限文本副本，时长限制在100到5000毫秒。
void OledUi_PopupOpen(OledUiPopup_t *popup, const char *title, const char *body,
    bool confirm, uint32_t now, uint32_t duration);
//弹窗到达持续时间后结束显示
void OledUi_PopupTick(OledUiPopup_t *popup, uint32_t now);
//活动弹窗消费确认或返回事件并阻止事件传至底页
bool OledUi_PopupEvent(OledUiPopup_t *popup, uint32_t event, uint32_t now);

#endif
