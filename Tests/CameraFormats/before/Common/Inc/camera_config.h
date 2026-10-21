#ifndef CAMERA_CONFIG_H
#define CAMERA_CONFIG_H

#include <stdint.h>

//当前只支持8位灰度输出
typedef enum
{
	CAMERA_PIXEL_FORMAT_Y8 = 0U,
    CAMERA_PIXEL_FORMAT_RGB565,
    CAMERA_PIXEL_FORMAT_YUV422
} CameraPixelFormat_t;

//摄像头输出配置，由Application统一选择
typedef struct
{
	uint16_t width;
	uint16_t height;
	CameraPixelFormat_t format;
} CameraConfig_t;

//描述一张完整图像
typedef struct
{
    uint16_t width;
    uint16_t height;

    CameraPixelFormat_t format;

    const uint8_t *data;
    uint32_t size_bytes;

} CameraFrame_t;

#endif
