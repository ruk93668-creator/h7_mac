#include "app_uvc_test.h"
#include "usb_mode.h"
#include "usb_uvc.h"
#include <stdbool.h>

#define UVC_TEST_WIDTH        160U
#define UVC_TEST_HEIGHT       120U
#define UVC_TEST_FRAME_BYTES  (UVC_TEST_WIDTH * UVC_TEST_HEIGHT * 2U)

//YUY2顺序为Y0、U、Y1、V，八条等宽的BT.601有限范围彩条
static const uint8_t s_color_bars[8][4] =
{
    {235U, 128U, 235U, 128U},//白
    {210U, 16U, 210U, 146U}, //黄
    {170U, 166U, 170U, 16U}, //青
    {145U, 54U, 145U, 34U},  //绿
    {106U, 202U, 106U, 222U},//品红
    {81U, 90U, 81U, 240U},  //红
    {41U, 240U, 41U, 110U}, //蓝
    {16U, 128U, 16U, 128U}  //黑
};

//USB使用PIO读取静态图像，不占用摄像头或SDRAM
static uint8_t s_test_frame[UVC_TEST_FRAME_BYTES] __attribute__((aligned(4)));
static bool s_initialized;
static bool s_pattern_ready;

//仅记录本模块提交结果，不替代USB的资源所有权
static volatile SysResult_t s_last_result;

//填充固定彩条，生成后不再改写图像
static void UvcTest_GeneratePattern(void);

SysResult_t App_UvcTestInit(void)
{
    if (s_initialized)
        return SYS_OK;

    //TEST模式不生成图像，也不调用UVC提交接口
    if (USB_Mode_GetMode() == USB_MODE_UVC)
    {
        UvcTest_GeneratePattern();
        s_pattern_ready = true;
    }

    s_initialized = true;
    s_last_result = SYS_OK;
    return SYS_OK;
}

void App_UvcTestRun(void)
{
    UsbUvcStreamStatus_t status;

    if (!s_initialized || !s_pattern_ready || USB_Mode_GetMode() != USB_MODE_UVC)
        return;

    //Reset释放Class或Abort失败时，均不能假定图像已归还
    if (USB_UVC_GetStreamStatus(&status) != SYS_OK)
        return;

    //Class统一检查Commit、Alt1、取消及上一帧的所有权
    if (!status.can_submit || status.frame_owned)
        return;

    //10FPS由既有SOF调度保证，此处不增加Timer或阻塞延时
    s_last_result = USB_UVC_SubmitFrame(s_test_frame, sizeof(s_test_frame));
}

//内部功能函数实现

static void UvcTest_GeneratePattern(void)
{
    uint32_t y;
    uint32_t x;
    uint32_t offset;
    uint32_t color;

    for (y = 0U; y < UVC_TEST_HEIGHT; y++)
    {
        for (x = 0U; x < UVC_TEST_WIDTH; x += 2U)
        {
            //每两个像素共享一组UV，不跨行或跨彩条取样
            color = x / (UVC_TEST_WIDTH / 8U);
            offset = (y * UVC_TEST_WIDTH + x) * 2U;
            s_test_frame[offset] = s_color_bars[color][0];
            s_test_frame[offset + 1U] = s_color_bars[color][1];
            s_test_frame[offset + 2U] = s_color_bars[color][2];
            s_test_frame[offset + 3U] = s_color_bars[color][3];
        }
    }
}
