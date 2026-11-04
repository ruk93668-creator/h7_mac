#include "video_config.h"

//阶段01使用TEST验收控制面，不启动相机与UVC视频
static const VideoConfig_t s_default_config =
{
    .camera =
    {
        .width = 160U,
        .height = 120U,
        .format = CAMERA_PIXEL_FORMAT_YUV422
    },
    .output = VIDEO_OUTPUT_USB_TEST,
    .output_fps = 25U
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
