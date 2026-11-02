#include "video_config.h"

//唯一的用户视频配置入口，不设置传感器采集帧率
static const VideoConfig_t s_default_config =
{
    .camera =
    {
        .width = 160U,
        .height = 120U,
        .format = CAMERA_PIXEL_FORMAT_YUV422
    },
    .output = VIDEO_OUTPUT_USB_UVC,
    .output_fps = 10U
};

const VideoConfig_t *VideoConfig_GetDefault(void)
{
    return &s_default_config;
}

SysResult_t VideoConfig_RequestUpdate(const VideoConfig_t *config)
{
    if (config == NULL)
        return SYS_ERR_INVALID_ARG;

    return SYS_ERR_NOT_SUPPORTED;
}
