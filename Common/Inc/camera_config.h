#ifndef CAMERA_CONFIG_H
#define CAMERA_CONFIG_H

#include <stdint.h>

//固定长度图像格式，开机时选择
typedef enum
{
	CAMERA_PIXEL_FORMAT_Y8 = 0U, //每像素1字节灰度
	CAMERA_PIXEL_FORMAT_RGB565, //每像素2字节，低字节在前
	CAMERA_PIXEL_FORMAT_YUV422  //Y0 U0 Y1 V0，两个像素4字节
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
