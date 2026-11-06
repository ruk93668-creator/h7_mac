#include "video_config.h"

#ifndef APP_VIDEO_TEST_MODE
#define APP_VIDEO_TEST_MODE 0
#endif

#if APP_VIDEO_TEST_MODE != 0 && APP_VIDEO_TEST_MODE != 1
#error APP_VIDEO_TEST_MODE_must_be_zero_or_one
#endif

#ifndef APP_VIDEO_MJPEG_MODE
#define APP_VIDEO_MJPEG_MODE 0
#endif

#if APP_VIDEO_MJPEG_MODE != 0 && APP_VIDEO_MJPEG_MODE != 1
#error APP_VIDEO_MJPEG_MODE_must_be_zero_or_one
#endif

//保存唯一默认视频配置，TEST构建只关闭视频链路
static const VideoConfig_t s_default_config =
{
    .camera =
    {
        .width = APP_VIDEO_MJPEG_MODE ? 320U : 160U,
        .height = APP_VIDEO_MJPEG_MODE ? 240U : 120U,
        .format = APP_VIDEO_MJPEG_MODE ? CAMERA_PIXEL_FORMAT_JPEG : CAMERA_PIXEL_FORMAT_YUV422
    },
    .output = APP_VIDEO_TEST_MODE ? VIDEO_OUTPUT_USB_TEST : VIDEO_OUTPUT_USB_UVC,
    .output_fps = 25U,
    .output_max_frame_bytes = APP_VIDEO_MJPEG_MODE ? 38400U : 0U
};

//取得静态默认视频配置
const VideoConfig_t *VideoConfig_GetDefault(void)
{

    //只返回静态配置地址，调用者不修改或释放该对象
    return &s_default_config;
}

//拒绝尚未支持的运行时配置更新
SysResult_t VideoConfig_RequestUpdate(const VideoConfig_t *config)
{

    //先检查配置指针，再明确拒绝尚未实现的运行中更新
    if (config == NULL)
        return SYS_ERR_INVALID_ARG;

    return SYS_ERR_NOT_SUPPORTED;
}
