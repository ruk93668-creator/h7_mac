//OLED界面只使用IPC本地副本，通过Device Ops绘图，不访问共享内存或HAL
#include "oled_ui.h"
#include "ipc_runtime.h"
#include <stddef.h>
#include <string.h>

//每个字符占6列8行，其中5列保存字形，末列和末行留空
#define OLED_UI_CELL_WIDTH 6U
#define OLED_UI_CELL_HEIGHT 8U

//保存可打印ASCII的5列字形，位0对应字符顶部
static const uint8_t s_ascii_font[95][5] =
{
    {0x00U, 0x00U, 0x00U, 0x00U, 0x00U},//空格
    {0x00U, 0x00U, 0x5FU, 0x00U, 0x00U},//!
    {0x00U, 0x07U, 0x00U, 0x07U, 0x00U},//"
    {0x14U, 0x7FU, 0x14U, 0x7FU, 0x14U},//#
    {0x24U, 0x2AU, 0x7FU, 0x2AU, 0x12U},//$
    {0x23U, 0x13U, 0x08U, 0x64U, 0x62U},//%
    {0x36U, 0x49U, 0x55U, 0x22U, 0x50U},//&
    {0x00U, 0x05U, 0x03U, 0x00U, 0x00U},//'
    {0x00U, 0x1CU, 0x22U, 0x41U, 0x00U},//(
    {0x00U, 0x41U, 0x22U, 0x1CU, 0x00U},//)
    {0x14U, 0x08U, 0x3EU, 0x08U, 0x14U},//*
    {0x08U, 0x08U, 0x3EU, 0x08U, 0x08U},//+
    {0x00U, 0x50U, 0x30U, 0x00U, 0x00U},//,
    {0x08U, 0x08U, 0x08U, 0x08U, 0x08U},//-
    {0x00U, 0x60U, 0x60U, 0x00U, 0x00U},//.
    {0x20U, 0x10U, 0x08U, 0x04U, 0x02U},///
    {0x3EU, 0x51U, 0x49U, 0x45U, 0x3EU},//0
    {0x00U, 0x42U, 0x7FU, 0x40U, 0x00U},//1
    {0x42U, 0x61U, 0x51U, 0x49U, 0x46U},//2
    {0x21U, 0x41U, 0x45U, 0x4BU, 0x31U},//3
    {0x18U, 0x14U, 0x12U, 0x7FU, 0x10U},//4
    {0x27U, 0x45U, 0x45U, 0x45U, 0x39U},//5
    {0x3CU, 0x4AU, 0x49U, 0x49U, 0x30U},//6
    {0x01U, 0x71U, 0x09U, 0x05U, 0x03U},//7
    {0x36U, 0x49U, 0x49U, 0x49U, 0x36U},//8
    {0x06U, 0x49U, 0x49U, 0x29U, 0x1EU},//9
    {0x00U, 0x36U, 0x36U, 0x00U, 0x00U},//:
    {0x00U, 0x56U, 0x36U, 0x00U, 0x00U},//;
    {0x08U, 0x14U, 0x22U, 0x41U, 0x00U},//<
    {0x14U, 0x14U, 0x14U, 0x14U, 0x14U},//=
    {0x00U, 0x41U, 0x22U, 0x14U, 0x08U},//>
    {0x02U, 0x01U, 0x51U, 0x09U, 0x06U},//?
    {0x32U, 0x49U, 0x79U, 0x41U, 0x3EU},//@
    {0x7EU, 0x11U, 0x11U, 0x11U, 0x7EU},//A
    {0x7FU, 0x49U, 0x49U, 0x49U, 0x36U},//B
    {0x3EU, 0x41U, 0x41U, 0x41U, 0x22U},//C
    {0x7FU, 0x41U, 0x41U, 0x22U, 0x1CU},//D
    {0x7FU, 0x49U, 0x49U, 0x49U, 0x41U},//E
    {0x7FU, 0x09U, 0x09U, 0x09U, 0x01U},//F
    {0x3EU, 0x41U, 0x49U, 0x49U, 0x7AU},//G
    {0x7FU, 0x08U, 0x08U, 0x08U, 0x7FU},//H
    {0x00U, 0x41U, 0x7FU, 0x41U, 0x00U},//I
    {0x20U, 0x40U, 0x41U, 0x3FU, 0x01U},//J
    {0x7FU, 0x08U, 0x14U, 0x22U, 0x41U},//K
    {0x7FU, 0x40U, 0x40U, 0x40U, 0x40U},//L
    {0x7FU, 0x02U, 0x0CU, 0x02U, 0x7FU},//M
    {0x7FU, 0x04U, 0x08U, 0x10U, 0x7FU},//N
    {0x3EU, 0x41U, 0x41U, 0x41U, 0x3EU},//O
    {0x7FU, 0x09U, 0x09U, 0x09U, 0x06U},//P
    {0x3EU, 0x41U, 0x51U, 0x21U, 0x5EU},//Q
    {0x7FU, 0x09U, 0x19U, 0x29U, 0x46U},//R
    {0x46U, 0x49U, 0x49U, 0x49U, 0x31U},//S
    {0x01U, 0x01U, 0x7FU, 0x01U, 0x01U},//T
    {0x3FU, 0x40U, 0x40U, 0x40U, 0x3FU},//U
    {0x1FU, 0x20U, 0x40U, 0x20U, 0x1FU},//V
    {0x3FU, 0x40U, 0x38U, 0x40U, 0x3FU},//W
    {0x63U, 0x14U, 0x08U, 0x14U, 0x63U},//X
    {0x07U, 0x08U, 0x70U, 0x08U, 0x07U},//Y
    {0x61U, 0x51U, 0x49U, 0x45U, 0x43U},//Z
    {0x00U, 0x7FU, 0x41U, 0x41U, 0x00U},//[
    {0x02U, 0x04U, 0x08U, 0x10U, 0x20U},//反斜杠
    {0x00U, 0x41U, 0x41U, 0x7FU, 0x00U},//]
    {0x04U, 0x02U, 0x01U, 0x02U, 0x04U},//^
    {0x40U, 0x40U, 0x40U, 0x40U, 0x40U},//_
    {0x00U, 0x01U, 0x02U, 0x04U, 0x00U},//`
    {0x20U, 0x54U, 0x54U, 0x54U, 0x78U},//a
    {0x7FU, 0x48U, 0x44U, 0x44U, 0x38U},//b
    {0x38U, 0x44U, 0x44U, 0x44U, 0x20U},//c
    {0x38U, 0x44U, 0x44U, 0x48U, 0x7FU},//d
    {0x38U, 0x54U, 0x54U, 0x54U, 0x18U},//e
    {0x08U, 0x7EU, 0x09U, 0x01U, 0x02U},//f
    {0x0CU, 0x52U, 0x52U, 0x52U, 0x3EU},//g
    {0x7FU, 0x08U, 0x04U, 0x04U, 0x78U},//h
    {0x00U, 0x44U, 0x7DU, 0x40U, 0x00U},//i
    {0x20U, 0x40U, 0x44U, 0x3DU, 0x00U},//j
    {0x7FU, 0x10U, 0x28U, 0x44U, 0x00U},//k
    {0x00U, 0x41U, 0x7FU, 0x40U, 0x00U},//l
    {0x7CU, 0x04U, 0x18U, 0x04U, 0x78U},//m
    {0x7CU, 0x08U, 0x04U, 0x04U, 0x78U},//n
    {0x38U, 0x44U, 0x44U, 0x44U, 0x38U},//o
    {0x7CU, 0x14U, 0x14U, 0x14U, 0x08U},//p
    {0x08U, 0x14U, 0x14U, 0x18U, 0x7CU},//q
    {0x7CU, 0x08U, 0x04U, 0x04U, 0x08U},//r
    {0x48U, 0x54U, 0x54U, 0x54U, 0x20U},//s
    {0x04U, 0x3FU, 0x44U, 0x40U, 0x20U},//t
    {0x3CU, 0x40U, 0x40U, 0x20U, 0x7CU},//u
    {0x1CU, 0x20U, 0x40U, 0x20U, 0x1CU},//v
    {0x3CU, 0x40U, 0x30U, 0x40U, 0x3CU},//w
    {0x44U, 0x28U, 0x10U, 0x28U, 0x44U},//x
    {0x0CU, 0x50U, 0x50U, 0x50U, 0x3CU},//y
    {0x44U, 0x64U, 0x54U, 0x4CU, 0x44U},//z
    {0x00U, 0x08U, 0x36U, 0x41U, 0x00U},//{
    {0x00U, 0x00U, 0x7FU, 0x00U, 0x00U},//|
    {0x00U, 0x41U, 0x36U, 0x08U, 0x00U},//}
    {0x08U, 0x04U, 0x08U, 0x10U, 0x08U},//~
};

//把完整IPC副本排成四行文本，失效数据不显示为实时数值
static void OledUi_BuildLines(const IpcApplicationView_t *view,
    char lines[OLED_UI_ROWS][OLED_UI_COLUMNS + 1U]);

//向一行追加文字，超出21列的部分直接截断
static void OledUi_Append(char *line, const char *text);

//以十进制追加无符号数，不使用动态内存或printf
static void OledUi_AppendUnsigned(char *line, uint32_t value);

//把实测milli-FPS显示为一位小数，大值显示上限提示
static void OledUi_AppendFps(char *line, uint32_t fps_milli);

//按有效性、新鲜度和停止状态追加某一路FPS
static void OledUi_AppendRate(char *line, const IpcApplicationView_t *view,
    uint32_t source, uint32_t state, uint32_t fps);

//追加错误码，负数最小值也按无符号幅值安全转换
static void OledUi_AppendResult(char *line, int32_t result);

//将正式IPC格式编号转为界面名称
static const char *OledUi_Format(uint32_t format);

//将Control协调状态转为简短名称
static const char *OledUi_Control(uint32_t state);

//逐像素绘制一行文本，并限制坐标和字符串读取范围
static SysResult_t OledUi_DrawText(OledDevice_t *oled,
    uint16_t x, uint16_t y, const char *text);

//根据完整IPC副本绘制四行监控画面
SysResult_t OledUi_Render(OledDevice_t *oled, const IpcApplicationView_t *view)
{
    char lines[OLED_UI_ROWS][OLED_UI_COLUMNS + 1U] = {{0}};
    SysResult_t result;
    uint32_t row;

    //先校验输入和绘图接口，不能清空一个无法重新绘制的画面
    if (oled == NULL || view == NULL)
        return SYS_ERR_INVALID_ARG;

    if (oled->ops == NULL || oled->ops->clear == NULL || oled->ops->draw_pixel == NULL)
        return SYS_ERR_NOT_SUPPORTED;

    //发送期间设备还会逐页读取Framebuffer，此时禁止任何绘图
    if (oled->refresh_state != OLED_REFRESH_IDLE)
        return SYS_ERR_BUSY;

    //先完成排版，再清屏，整张画面只使用这一份IPC快照
    OledUi_BuildLines(view, lines);
    result = oled->ops->clear(oled);
    if (result != SYS_OK)
        return result;

    //每行固定占8像素，清屏保证短文本替换长文本时没有残留
    for (row = 0U; row < OLED_UI_ROWS; row++)
    {
        result = OledUi_DrawText(oled, 0U,
            (uint16_t)(row * OLED_UI_CELL_HEIGHT), lines[row]);
        if (result != SYS_OK)
            return result;
    }

    return SYS_OK;
}

//把完整IPC副本排成四行文本
static void OledUi_BuildLines(const IpcApplicationView_t *view,
    char lines[OLED_UI_ROWS][OLED_UI_COLUMNS + 1U])
{
    const IpcApplicationStatus_t *a = &view->application;

    //通信尚未建立时只显示启动进度，不拿缓存FPS冒充实时数据
    if (view->peer_state == IPC_PEER_STARTING)
    {
        OledUi_Append(lines[0], "IPC INITIALIZING");
        OledUi_Append(lines[1], "WAIT M7 HEARTBEAT");
        return;
    }

    //对端不在线时优先报告通信状态，旧业务数值全部隐藏
    if (view->peer_state != IPC_PEER_ONLINE)
    {
        if (view->peer_state == IPC_PEER_PROTOCOL_ERROR)
            OledUi_Append(lines[0], "IPC PROTOCOL ERROR");
        else if (view->peer_state == IPC_PEER_TIMEOUT ||
            view->peer_state == IPC_PEER_STARTUP_TIMEOUT)
            OledUi_Append(lines[0], "M7 OFFLINE");
        else
            OledUi_Append(lines[0], "IPC INVALID STATE");

        OledUi_Append(lines[1], view->peer_state == IPC_PEER_STARTUP_TIMEOUT ?
            "STARTUP TIMEOUT" : "CHECK IPC LINK");
        return;
    }

    //短暂BUSY可使用未过期缓存，其他读取错误不能展示实时数据
    if (view->read_result != SYS_OK && view->read_result != SYS_ERR_BUSY)
    {
        OledUi_Append(lines[0], "IPC READ ERROR");
        OledUi_AppendResult(lines[1], view->read_result);
        return;
    }

    //从未获得有效来源时显示无效，不能把全零结构解释为停止状态
    if ((view->valid_mask & IPC_APP_ALL) == 0U)
    {
        OledUi_Append(lines[0], "DATA INVALID");
        OledUi_Append(lines[1], "WAIT M7 STATUS");
        return;
    }

    //Camera配置只在该来源有效且新鲜时显示
    if ((view->valid_mask & IPC_APP_CAMERA) == 0U)
        OledUi_Append(lines[0], "CAM INVALID");
    else if ((view->fresh_mask & IPC_APP_CAMERA) == 0U)
        OledUi_Append(lines[0], "CAM STALE");
    else
    {
        OledUi_Append(lines[0], "CAM ");
        OledUi_AppendUnsigned(lines[0], a->width);
        OledUi_Append(lines[0], "x");
        OledUi_AppendUnsigned(lines[0], a->height);
        OledUi_Append(lines[0], " ");
        OledUi_Append(lines[0], OledUi_Format(a->format));
    }

    //采集和USB分别判断来源状态，USB任务启动不代表Windows已经收流
    OledUi_Append(lines[1], "CAP ");
    OledUi_AppendRate(lines[1], view, IPC_APP_CAMERA, a->camera_state, a->camera_fps_milli);
    OledUi_Append(lines[1], " USB ");
    OledUi_AppendRate(lines[1], view, IPC_APP_USB, a->usb_state, a->usb_fps_milli);

    //丢帧数也需要Camera新鲜度，不能在失联或陈旧时继续展示旧计数
    OledUi_Append(lines[2], "DROP ");
    if ((view->valid_mask & IPC_APP_CAMERA) == 0U)
        OledUi_Append(lines[2], "INVALID");
    else if ((view->fresh_mask & IPC_APP_CAMERA) == 0U)
        OledUi_Append(lines[2], "STALE");
    else
        OledUi_AppendUnsigned(lines[2], a->dropped_frames);

    //故障只从有效且新鲜的来源读取，过期故障也不能冒充当前状态
    if ((view->valid_mask & view->fresh_mask & IPC_APP_CONTROL) != 0U &&
        (a->control_state == 6U || a->control_result < SYS_OK))
    {
        OledUi_Append(lines[3], "FAULT CTRL ");
        OledUi_AppendResult(lines[3], a->control_result < SYS_OK ?
            a->control_result : a->last_fault);
    }
    else if ((view->valid_mask & view->fresh_mask & IPC_APP_CAMERA) != 0U &&
        a->camera_result < SYS_OK)
    {
        OledUi_Append(lines[3], "FAULT CAM ");
        OledUi_AppendResult(lines[3], a->camera_result);
    }
    else if ((view->valid_mask & view->fresh_mask & IPC_APP_USB) != 0U &&
        a->usb_result < SYS_OK)
    {
        OledUi_Append(lines[3], "FAULT USB ");
        OledUi_AppendResult(lines[3], a->usb_result);
    }
    else
    {
        OledUi_Append(lines[3], "M7 OK CTRL ");
        if ((view->valid_mask & IPC_APP_CONTROL) == 0U)
            OledUi_Append(lines[3], "INVALID");
        else if ((view->fresh_mask & IPC_APP_CONTROL) == 0U)
            OledUi_Append(lines[3], "STALE");
        else
            OledUi_Append(lines[3], OledUi_Control(a->control_state));
    }
}

//向一行追加文字并截断
static void OledUi_Append(char *line, const char *text)
{
    uint32_t length = 0U;

    //行缓冲始终保留结束符，不向屏幕外或字符串外继续写入
    while (length < OLED_UI_COLUMNS && line[length] != '\0')
        length++;

    while (length < OLED_UI_COLUMNS && *text != '\0')
        line[length++] = *text++;

    line[length] = '\0';
}

//以十进制追加无符号数
static void OledUi_AppendUnsigned(char *line, uint32_t value)
{
    char digits[11];
    uint32_t count = 0U;

    //先倒序取余，最多10位，零值也必须输出一个字符
    do
    {
        digits[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U);

    //逐位正序追加，统一由行宽检查负责裁剪
    while (count != 0U)
    {
        char text[2] = {digits[--count], '\0'};
        OledUi_Append(line, text);
    }
}

//把实测milli-FPS显示为一位小数
static void OledUi_AppendFps(char *line, uint32_t fps_milli)
{

    //FPS取一位小数且不四舍五入，大值用提示避免挤占另一来源
    if (fps_milli >= 100000U)
    {
        OledUi_Append(line, ">99");
        return;
    }

    OledUi_AppendUnsigned(line, fps_milli / 1000U);
    OledUi_Append(line, ".");
    OledUi_AppendUnsigned(line, (fps_milli % 1000U) / 100U);
}

//按有效性、新鲜度和停止状态追加某一路FPS
static void OledUi_AppendRate(char *line, const IpcApplicationView_t *view,
    uint32_t source, uint32_t state, uint32_t fps)
{

    //有效与新鲜必须分别满足，缺失或陈旧时不显示旧FPS
    if ((view->valid_mask & source) == 0U)
        OledUi_Append(line, "--");
    else if ((view->fresh_mask & source) == 0U)
        OledUi_Append(line, "STALE");
    else if (state == 0U)
        OledUi_Append(line, "STOP");
    else if (state == 2U)
        OledUi_Append(line, "HALT");
    else if (source == IPC_APP_USB && fps == 0U)
        OledUi_Append(line, "WAIT");
    else
        OledUi_AppendFps(line, fps);
}

//追加带符号操作结果
static void OledUi_AppendResult(char *line, int32_t result)
{

    //以无符号方式取负数幅值，避免INT32_MIN取负溢出
    if (result < 0)
        OledUi_Append(line, "-");

    OledUi_AppendUnsigned(line, result < 0 ? 0U - (uint32_t)result : (uint32_t)result);
}

//将正式IPC格式编号转为界面名称
static const char *OledUi_Format(uint32_t format)
{
    switch (format)
    {
        case IPC_IMAGE_Y8: return "Y8";
        case IPC_IMAGE_RGB565: return "RGB565";
        case IPC_IMAGE_YUY2: return "YUY2";
        case IPC_IMAGE_JPEG: return "MJPG";
        default: return "UNKNOWN";
    }
}

//将Control协调状态转为简短名称
static const char *OledUi_Control(uint32_t state)
{
    switch (state)
    {
        case 0U: return "IDLE";
        case 1U: return "CAM START";
        case 2U: return "USB START";
        case 3U: return "RUN";
        case 4U: return "USB STOP";
        case 5U: return "CAM STOP";
        case 6U: return "ERROR";
        default: return "INVALID";
    }
}

//逐像素绘制一行文本
static SysResult_t OledUi_DrawText(OledDevice_t *oled,
    uint16_t x, uint16_t y, const char *text)
{
    uint32_t index, column, bit;
    uint8_t character;
    SysResult_t result;

    //字符数和像素坐标都有限制，外部文本不会造成Framebuffer越界
    for (index = 0U; index < OLED_UI_COLUMNS && text[index] != '\0'; index++)
    {
        character = (uint8_t)text[index];
        if (character < 32U || character > 126U)
            character = (uint8_t)'?';

        //每个像素都经过Device绘图接口，未使用的第六列保持熄灭
        for (column = 0U; column < OLED_UI_CELL_WIDTH; column++)
        {
            uint32_t px = (uint32_t)x + index * OLED_UI_CELL_WIDTH + column;
            if (px >= OLED_WIDTH)
                break;

            for (bit = 0U; bit < OLED_UI_CELL_HEIGHT; bit++)
            {
                uint32_t py = (uint32_t)y + bit;
                bool on = column < 5U && (s_ascii_font[character - 32U][column] & (1U << bit)) != 0U;
                if (py >= OLED_HEIGHT)
                    break;

                result = oled->ops->draw_pixel(oled, (uint16_t)px, (uint16_t)py, on);
                if (result != SYS_OK)
                    return result;
            }
        }
    }

    return SYS_OK;
}
