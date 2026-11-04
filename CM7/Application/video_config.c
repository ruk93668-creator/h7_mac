#include "video_config.h"

//保存唯一默认视频配置，本阶段使用USB TEST控制面
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
