#ifndef VIDEO_CONFIG_TYPES_H
#define VIDEO_CONFIG_TYPES_H

#include "camera_config.h"

typedef enum
{
    VIDEO_OUTPUT_NONE = 0,
    VIDEO_OUTPUT_USB_UVC,
    VIDEO_OUTPUT_ETH,
    VIDEO_OUTPUT_WIFI,
    VIDEO_OUTPUT_USB_TEST
} VideoOutput_t;

//输出模块接收的视频参数，不包含业务输出选择
typedef struct
{
    CameraConfig_t camera;
    uint16_t fps;

    //压缩视频的单帧传输上限，固定长度格式忽略此字段
    uint32_t max_frame_bytes;
} VideoStreamConfig_t;

//业务默认配置，采集时序继续由传感器现有驱动管理
typedef struct
{
    CameraConfig_t camera;
    VideoOutput_t output;
    uint16_t output_fps;

    //JPEG输出单帧上限，不代表每帧实际长度
    uint32_t output_max_frame_bytes;
} VideoConfig_t;

#endif
