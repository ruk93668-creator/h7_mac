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

    //输出目标25FPS，实际采集与主机接收速率另行测量
    .output_fps =  25U,

    //JPEG帧长由配置限定，25FPS时必须满足FS每40ms的包数预算
    .output_max_frame_bytes = APP_VIDEO_MJPEG_MODE ? 40000U : 0U
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

//SD录像策略独立于USB目标FPS，不设置传感器时序
static const AppStorageConfig_t s_storage_config =
{
    .record_fps = 25U,

    //当前SD卡使用第三个MBR分区，未格式化或改动卡布局
    .partition = 3U,

    .segment_bytes = 4UL * 1024UL * 1024UL,

    .segment_ms = 60000U,

    .total_bytes = 16UL * 1024UL * 1024UL,

    .reserve_bytes = 512UL * 1024UL,

    .max_segments = 16U
};

//提供静态只读策略，开始保存仍需显式业务请求
const AppStorageConfig_t *VideoConfig_GetStorage(void)
{
    return &s_storage_config;
}
